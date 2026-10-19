#include "abox_boot_v2_app.h"
#include "abox_platform_port.h"

#include <stdio.h>
#include <string.h>

#define ABOX_BOOT_V2_CA_FILE "ota_ca.pem"
#define ABOX_BOOT_V2_PROVISION_RETRY_1_MS 5000U
#define ABOX_BOOT_V2_PROVISION_RETRY_2_MS 15000U
#define ABOX_BOOT_V2_PROVISION_RETRY_3_MS 60000U
#define ABOX_BOOT_V2_TERMINAL_RETRY_MS 300000U

typedef enum {
    ST_WAIT,
    ST_PROBE,
    ST_CA_OPEN,
    ST_CA_QUERY,
    ST_CA_READ,
    ST_CA_CLOSE,
    ST_CA_DELETE,
    ST_CA_UPLOAD,
    ST_IDLE,
    ST_MQTT_QUIESCE,
    ST_MQTT_DISC,
    ST_MQTT_CLOSE,
    ST_DOWNLOADER,
    ST_HTTP_STOP,
    ST_PDP_DEACT,
    ST_PDP_ACT,
    ST_TLS_VERSION,
    ST_TLS_LEVEL,
    ST_TLS_SNI,
    ST_TLS_CA,
    ST_HTTP_CONTEXT,
    ST_HTTP_TLS,
    ST_URL,
    ST_FILE_DELETE,
    ST_FILE_OPEN,
    ST_RANGE_GET,
    ST_HTTP_READ,
    ST_FILE_WRITE,
    ST_FILE_CLOSE,
    ST_FILE_LIST,
    ST_VERIFY_OPEN,
    ST_VERIFY_READ,
    ST_VERIFY_CLOSE,
    ST_VERIFY,
    ST_RETRY,
    ST_ERROR
} State;

typedef struct {
    ABoxBootV2AppPort port;
    ABoxBootV2AppConfig cfg;
    State state;
    uint8_t provisioned;
    uint8_t busy;
    uint8_t seeding;
    uint8_t slot;
    uint8_t handle_valid;
    uint8_t install_ready;
    uint8_t failure_pending;
    uint8_t payload_sent;
    uint8_t vector[8];
    uint8_t vector_len;
    uint8_t probe_index;
    uint8_t provision_retry_count;
    uint8_t ca_repair_attempted;
    uint8_t ca_repair_pending;
    uint8_t seed_attempted;
    uint8_t stable_checked;
    uint8_t file_seen;
    uint32_t handle;
    uint32_t expected_size;
    uint32_t expected_crc;
    uint32_t downloaded;
    uint32_t chunk_size;
    uint32_t chunk_received;
    uint32_t read_size;
    uint32_t crc;
    uint32_t ca_read_size;
    uint32_t ca_crc;
    uint32_t http_status;
    uint32_t http_length;
    uint32_t written;
    uint32_t written_total;
    uint32_t file_size;
    uint32_t last_error;
    uint8_t mqtt_client_index;
    uint8_t workspace_borrowed;
    char last_command[64];
    uint32_t retry_due;
    char url[ABOX_BOOT_V2_APP_URL_SIZE];
    char version[ABOX_BOOT_V2_APP_VERSION_SIZE];
    ABoxHttpsUfsDownloader downloader;
    ABoxHttpsUfsDoneFn downloader_done;
    void *downloader_done_user;
    uint8_t direct_supported;
} Context;

/* Product linker scripts place this reset-scratch context after the fixed
 * fault mailbox so it does not consume normal BSS. */
static Context g __attribute__((section(".ota_work")));
/* Unlike OTA scratch, normal BSS is cleared by every App startup. */
static uint8_t g_initialized;
const char g_abox_boot_v2_stable_seed_source_marker[]
    __attribute__((used)) = "ABOX_STABLE_SEED_SOURCE=" ABOX_BOOT_V2_STABLE_SEED_SOURCE;

static const char *const probes[] = {
    "AT+QFLDS=\"UFS\"",
    "AT+QHTTPGET=?",
    "AT+QHTTPGETEX=?",
    "AT+QHTTPREAD=?",
    "AT+QHTTPREADFILE=?",
    "AT+QHTTPSTOP=?",
    "AT+QFLST=?",
    "AT+QFOPEN=?",
    "AT+QFREAD=?",
    "AT+QFWRITE=?",
    "AT+QFCLOSE=?",
    "AT+QFDEL=?",
    "AT+QFUPL=?",
    "AT+QSSLCFG=?",
    "AT+QHTTPCFG=?"
};

static void command_done(ABoxBootV2AtResult result, void *user);
static void download_fail(uint32_t error);
static void provision_fail(uint32_t error);

static const char *slot_name(uint8_t slot)
{
    return slot ? "fw_slot1.bin" : "fw_slot0.bin";
}

static uint32_t now(void)
{
    return g.port.tick_ms ? g.port.tick_ms(g.port.context) : 0U;
}

static void log_message(uint8_t level, const char *message)
{
    if (g.port.log) g.port.log(g.port.context, level, message);
}

static void log_failure(const char *kind, uint32_t error, const char *stage)
{
    char message[192];
    (void)snprintf(message, sizeof(message), "%s failed stage=%s code=%lu command=%s", kind, stage,
                   (unsigned long)error, g.last_command[0] ? g.last_command : "none");
    log_message(3U, message);
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    while (length--) {
        uint8_t bit;
        crc ^= *data++;
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

static int valid_config(const ABoxBootV2AppConfig *config)
{
    return config->ota_origin && strncmp(config->ota_origin, "https://", 8U) == 0 &&
           config->revision_product && config->artifact_name && config->version_prefix &&
           config->running_version && config->running_version[0] &&
           strlen(config->running_version) < ABOX_BOOT_V2_APP_VERSION_SIZE &&
           config->ca_pem && config->ca_pem_length &&
           config->app_size && config->app_size <= config->app_max_size;
}

static int provision_state(State state)
{
    return state >= ST_PROBE && state <= ST_CA_UPLOAD;
}

static const char *provision_stage(void)
{
    if (g.state == ST_PROBE && g.probe_index < (uint8_t)(sizeof(probes) / sizeof(probes[0])))
        return probes[g.probe_index];
    if (g.state == ST_CA_OPEN) return "CA_OPEN";
    if (g.state == ST_CA_QUERY) return "CA_QUERY";
    if (g.state == ST_CA_READ) return "CA_READ";
    if (g.state == ST_CA_CLOSE) return "CA_CLOSE";
    if (g.state == ST_CA_DELETE) return "CA_DELETE";
    if (g.state == ST_CA_UPLOAD) return "CA_UPLOAD";
    return "UNKNOWN";
}

static const char *download_stage(void)
{
    switch (g.state) {
    case ST_MQTT_QUIESCE: return "MQTT_QUIESCE";
    case ST_MQTT_DISC: return "MQTT_DISC";
    case ST_MQTT_CLOSE: return "MQTT_CLOSE";
    case ST_DOWNLOADER: return ABoxHttpsUfs_Phase(&g.downloader);
    case ST_HTTP_STOP: return "HTTP_STOP";
    case ST_PDP_DEACT: return "PDP_DEACT";
    case ST_PDP_ACT: return "PDP_ACT";
    case ST_TLS_VERSION: return "TLS_VERSION";
    case ST_TLS_LEVEL: return "TLS_LEVEL";
    case ST_TLS_SNI: return "TLS_SNI";
    case ST_TLS_CA: return "TLS_CA";
    case ST_HTTP_CONTEXT: return "HTTP_CONTEXT";
    case ST_HTTP_TLS: return "HTTP_TLS";
    case ST_URL: return "URL";
    case ST_FILE_DELETE: return "FILE_DELETE";
    case ST_FILE_OPEN: return "FILE_OPEN";
    case ST_RANGE_GET: return "RANGE_GET";
    case ST_HTTP_READ: return "HTTP_READ";
    case ST_FILE_WRITE: return "FILE_WRITE";
    case ST_FILE_CLOSE: return "FILE_CLOSE";
    case ST_FILE_LIST: return "FILE_LIST";
    case ST_VERIFY_OPEN: return "VERIFY_OPEN";
    case ST_VERIFY_READ: return "VERIFY_READ";
    case ST_VERIFY_CLOSE: return "VERIFY_CLOSE";
    case ST_VERIFY: return "VERIFY";
    default: return "UNKNOWN";
    }
}

static void downloader_bridge_done(ABoxBootV2AtResult result, void *user)
{
    if (!g_initialized) return;
    ABoxHttpsUfsDoneFn done = g.downloader_done;
    void *done_user = g.downloader_done_user;
    (void)user;
    g.downloader_done = 0;
    g.downloader_done_user = 0;
    if (done) done((ABoxHttpsUfsAtResult)result, done_user);
}

static uint32_t downloader_tick(void *context)
{
    (void)context;
    return now();
}

static int downloader_submit(void *context, const char *command,
                             uint32_t timeout_ms, ABoxHttpsUfsDoneFn done,
                             void *user)
{
    (void)context;
    (void)snprintf(g.last_command, sizeof(g.last_command), "%s", command);
    if (g.downloader_done || !g.port.submit) return 0;
    g.downloader_done = done;
    g.downloader_done_user = user;
    if (!g.port.submit(g.port.context, command, timeout_ms,
                       downloader_bridge_done, 0)) {
        g.downloader_done = 0;
        g.downloader_done_user = 0;
        return 0;
    }
    return 1;
}

static int downloader_send(void *context, const uint8_t *data, uint16_t length)
{
    (void)context;
    return g.port.send_payload &&
           g.port.send_payload(g.port.context, data, length);
}

static int downloader_raw(void *context, uint32_t length)
{
    (void)context;
    return g.port.begin_raw_read &&
           g.port.begin_raw_read(g.port.context, length);
}

static int downloader_pending(void *context)
{
    (void)context;
    return g.port.has_pending && g.port.has_pending(g.port.context);
}

static int downloader_active(void *context)
{
    (void)context;
    return g.port.is_active && g.port.is_active(g.port.context);
}

static void downloader_cancel(void *context)
{
    (void)context;
    g.downloader_done = 0;
    g.downloader_done_user = 0;
    if (g.port.cancel) g.port.cancel(g.port.context);
}

static void downloader_log(void *context, uint8_t level, const char *message)
{
    (void)context;
    log_message(level, message);
}

static uint32_t downloader_overflow(void *context)
{
    (void)context;
    return g.port.rx_overflow_count
               ? g.port.rx_overflow_count(g.port.context)
               : 0U;
}

static int downloader_vector(void *context, const uint8_t vector[8])
{
    (void)context;
    return ABoxBootV2_ImageVectorValid(vector);
}

static int ufs_name_matches(const char *reported, const char *expected)
{
    if (!reported || !expected) return 0;
    if (strncmp(reported, "UFS:", 4U) == 0) reported += 4U;
    return strcmp(reported, expected) == 0;
}

static int ca_matches(void)
{
    return g.ca_read_size == g.cfg.ca_pem_length &&
           (g.ca_crc ^ 0xFFFFFFFFU) == ABoxBootV2_Crc32(g.cfg.ca_pem, g.cfg.ca_pem_length);
}

static uint32_t provision_retry_delay(uint8_t retry_count)
{
    if (retry_count == 1U) return ABOX_BOOT_V2_PROVISION_RETRY_1_MS;
    if (retry_count == 2U) return ABOX_BOOT_V2_PROVISION_RETRY_2_MS;
    return ABOX_BOOT_V2_PROVISION_RETRY_3_MS;
}

static int submit_command(const char *command, uint32_t timeout_ms)
{
    (void)snprintf(g.last_command, sizeof(g.last_command), "%s", command);
    g.payload_sent = 0U;
    if (g.port.submit &&
        g.port.submit(g.port.context, command, timeout_ms, command_done, 0))
        return 1;

    if (provision_state(g.state))
        provision_fail(g.state == ST_PROBE ? ABOX_BOOT_V2_APP_ERROR_UNSUPPORTED
                                           : ABOX_BOOT_V2_APP_ERROR_CA);
    else
        download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
    return 0;
}

static void resume_mqtt(void)
{
    if (g.workspace_borrowed && g.port.workspace_return) {
        g.port.workspace_return(g.port.context);
        g.workspace_borrowed = 0U;
    }
    if (g.port.mqtt_pause) g.port.mqtt_pause(g.port.context, 0U);
}

static void provision_fail(uint32_t error)
{
    uint32_t delay;
    const char *stage = provision_stage();

    if (g.port.cancel) g.port.cancel(g.port.context);
    g.provisioned = 0U;
    g.handle_valid = 0U;
    g.last_error = error;
    ++g.provision_retry_count;
    log_failure("Boot V2 provisioning", error, stage);

    if (g.provision_retry_count <= 3U) {
        delay = provision_retry_delay(g.provision_retry_count);
        g.state = ST_RETRY;
    } else {
        delay = ABOX_BOOT_V2_TERMINAL_RETRY_MS;
        g.state = ST_ERROR;
    }
    g.retry_due = now() + delay;
}

static void download_fail(uint32_t error)
{
    const uint8_t was_seeding = g.seeding;
    const char *stage = download_stage();

    if (g.port.cancel) g.port.cancel(g.port.context);
    resume_mqtt();
    g.last_error = error;
    if (g.busy && !was_seeding) g.failure_pending = 1U;
    if (was_seeding) {
        g.seed_attempted = 0U;
        g.stable_checked = 0U;
    }
    g.busy = 0U;
    g.seeding = 0U;
    g.handle_valid = 0U;
    g.retry_due = now() + ABOX_BOOT_V2_TERMINAL_RETRY_MS;
    g.state = ST_ERROR;
    log_failure("Boot V2 staging", error, stage);
}

static void begin_ca_upload(void)
{
    char command[64];
    g.ca_repair_attempted = 1U;
    g.state = ST_CA_UPLOAD;
    (void)snprintf(command, sizeof(command), "AT+QFUPL=\"%s\",%lu,30",
                   ABOX_BOOT_V2_CA_FILE, (unsigned long)g.cfg.ca_pem_length);
    (void)submit_command(command, 40000U);
}

static void start_provisioning(void)
{
    if (g.port.cancel) g.port.cancel(g.port.context);
    g.provisioned = 0U;
    g.probe_index = 0U;
    g.ca_read_size = 0U;
    g.ca_crc = 0xFFFFFFFFU;
    g.ca_repair_attempted = 0U;
    g.ca_repair_pending = 0U;
    g.handle = 0U;
    g.handle_valid = 0U;
    g.state = ST_PROBE;
    (void)submit_command(probes[0], 5000U);
}

static void begin_download(void)
{
    char command[24];
    if (g.port.mqtt_pause) g.port.mqtt_pause(g.port.context, 1U);
    if (g.port.mqtt_stop) {
        g.state = ST_MQTT_QUIESCE;
        return;
    }
    g.mqtt_client_index = g.port.mqtt_client_index(g.port.context);
    g.state = ST_MQTT_DISC;
    (void)snprintf(command, sizeof(command), "AT+QMTDISC=%u",
                   (unsigned)g.mqtt_client_index);
    (void)submit_command(command, 10000U);
}

static int running_image_crc(uint32_t *result)
{
    uint32_t offset = 0U;
    uint32_t crc = 0xFFFFFFFFU;
    uint8_t scratch[16];
    uint8_t *buffer = g.port.mqtt_stop ? scratch : g.port.transfer_buffer;
    uint32_t capacity = g.port.mqtt_stop ? sizeof(scratch) :
                        g.port.transfer_buffer_size;
    if (!result || !g.port.transfer_buffer ||
        g.port.transfer_buffer_size < ABOX_BOOT_V2_APP_RAW_CHUNK)
        return 0;
    while (offset < g.cfg.app_size) {
        uint32_t length = g.cfg.app_size - offset;
        if (length > capacity) length = capacity;
        if (!ABox_PortFlashRead(g.cfg.app_start_addr + offset,
                                buffer, length))
            return 0;
        crc = crc_update(crc, buffer, (uint16_t)length);
        offset += length;
    }
    *result = crc ^ 0xFFFFFFFFU;
    return 1;
}

static int begin_local_seed(void)
{
    if (!g.provisioned || g.busy || !g.cfg.running_version ||
        strlen(g.cfg.running_version) == 0U ||
        strlen(g.cfg.running_version) >= ABOX_BOOT_V2_APP_VERSION_SIZE ||
        g.cfg.app_size == 0U || g.cfg.app_size > g.cfg.app_max_size) {
        g.last_error = ABOX_BOOT_V2_APP_ERROR_STATE;
        return 0;
    }
    g.seeding = 1U;
    g.slot = 0U;
    g.expected_size = g.cfg.app_size;
    if (!running_image_crc(&g.expected_crc)) {
        g.last_error = ABOX_BOOT_V2_APP_ERROR_STATE;
        return 0;
    }
    g.downloaded = 0U;
    g.read_size = 0U;
    g.crc = 0xFFFFFFFFU;
    g.vector_len = 0U;
    g.file_seen = 0U;
    g.file_size = 0U;
    g.handle_valid = 0U;
    g.failure_pending = 0U;
    g.busy = 1U;
    g.last_error = 0U;
    (void)snprintf(g.version, sizeof(g.version), "%s", g.cfg.running_version);
    begin_download();
    return 1;
}

static int begin_request(const ABoxBootV2AppRequest *request, uint8_t seeding)
{
    ABoxBootV2Record state;
    char expected[ABOX_BOOT_V2_APP_URL_SIZE];
    int length;

    if (!request || !g.provisioned || g.busy || request->size == 0U ||
        request->size > g.cfg.app_max_size || strlen(request->version) == 0U ||
        strlen(request->version) >= ABOX_BOOT_V2_APP_VERSION_SIZE ||
        strncmp(request->version, g.cfg.version_prefix, strlen(g.cfg.version_prefix)) != 0) {
        g.last_error = ABOX_BOOT_V2_APP_ERROR_URL;
        return 0;
    }

    length = snprintf(expected, sizeof(expected), "%s/fw-revisions/%s/%s/%s",
                      g.cfg.ota_origin, g.cfg.revision_product, request->version,
                      g.cfg.artifact_name);
    if (length <= 0 || (uint32_t)length >= sizeof(expected) ||
        strcmp(expected, request->url) != 0) {
        g.last_error = ABOX_BOOT_V2_APP_ERROR_URL;
        return 0;
    }

    memset(&state, 0, sizeof(state));
    if (ABoxBootV2_StateLoad(&state) && !seeding &&
        state.state != ABOX_BOOT_V2_NORMAL && state.state != ABOX_BOOT_V2_CONFIRMED) {
        g.last_error = ABOX_BOOT_V2_APP_ERROR_STATE;
        return 0;
    }

    g.seeding = seeding;
    g.slot = seeding ? 0U : (state.stable_slot == 0U ? 1U : 0U);
    g.expected_size = request->size;
    g.expected_crc = request->crc32;
    g.downloaded = 0U;
    g.read_size = 0U;
    g.crc = 0xFFFFFFFFU;
    g.vector_len = 0U;
    g.file_seen = 0U;
    g.file_size = 0U;
    g.handle_valid = 0U;
    g.failure_pending = 0U;
    g.busy = 1U;
    g.last_error = 0U;
    (void)snprintf(g.url, sizeof(g.url), "%s", request->url);
    (void)snprintf(g.version, sizeof(g.version), "%s", request->version);
    begin_download();
    return 1;
}

static void provision_ready(void)
{
    g.provisioned = 1U;
    g.provision_retry_count = 0U;
    g.state = ST_IDLE;
    if (!g.failure_pending) g.last_error = 0U;
    log_message(1U, "Boot V2 App UFS/HTTP/TLS/CA ready");
}

static void start_common_downloader(void)
{
    ABoxHttpsUfsRequest request;
    memset(&request, 0, sizeof(request));
    request.url = g.url;
    request.file = slot_name(g.slot);
    request.size = g.expected_size;
    request.crc32 = g.expected_crc;
    ABoxHttpsUfs_SetDirectSupported(&g.downloader, g.direct_supported);
    g.state = ST_DOWNLOADER;
    if (!ABoxHttpsUfs_Start(&g.downloader, &request))
        download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
}

static void command_succeeded(void)
{
    char command[128];

    switch (g.state) {
    case ST_PROBE:
        if (++g.probe_index < (uint8_t)(sizeof(probes) / sizeof(probes[0])))
            (void)submit_command(probes[g.probe_index], 5000U);
        else {
            g.state = ST_CA_OPEN;
            (void)submit_command("AT+QFOPEN=\"ota_ca.pem\",2", 5000U);
        }
        break;
    case ST_CA_OPEN:
        if (!g.handle_valid) {
            provision_fail(ABOX_BOOT_V2_APP_ERROR_CA);
            break;
        }
        g.ca_read_size = 0U;
        g.ca_crc = 0xFFFFFFFFU;
        g.state = ST_CA_READ;
        (void)snprintf(command, sizeof(command), "AT+QFREAD=%lu,%lu",
                       (unsigned long)g.handle,
                       (unsigned long)(g.cfg.ca_pem_length > ABOX_BOOT_V2_APP_RAW_CHUNK
                                           ? ABOX_BOOT_V2_APP_RAW_CHUNK
                                           : g.cfg.ca_pem_length));
        (void)submit_command(command, 10000U);
        break;
    case ST_CA_QUERY:
        if (g.handle_valid) {
            g.ca_repair_pending = 1U;
            g.state = ST_CA_CLOSE;
            (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu",
                           (unsigned long)g.handle);
            (void)submit_command(command, 5000U);
        } else {
            g.ca_repair_pending = 0U;
            begin_ca_upload();
        }
        break;
    case ST_CA_READ:
        if (g.ca_read_size < g.cfg.ca_pem_length) {
            uint32_t length = g.cfg.ca_pem_length - g.ca_read_size;
            if (length > ABOX_BOOT_V2_APP_RAW_CHUNK) length = ABOX_BOOT_V2_APP_RAW_CHUNK;
            (void)snprintf(command, sizeof(command), "AT+QFREAD=%lu,%lu",
                           (unsigned long)g.handle, (unsigned long)length);
            (void)submit_command(command, 10000U);
        } else {
            g.state = ST_CA_CLOSE;
            (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu",
                           (unsigned long)g.handle);
            (void)submit_command(command, 5000U);
        }
        break;
    case ST_CA_CLOSE:
        g.handle_valid = 0U;
        if (g.ca_repair_pending) {
            g.ca_repair_pending = 0U;
            g.state = ST_CA_DELETE;
            (void)submit_command("AT+QFDEL=\"ota_ca.pem\"", 5000U);
        } else if (ca_matches()) {
            provision_ready();
        } else if (g.ca_repair_attempted) {
            provision_fail(ABOX_BOOT_V2_APP_ERROR_CA);
        } else {
            g.state = ST_CA_DELETE;
            (void)submit_command("AT+QFDEL=\"ota_ca.pem\"", 5000U);
        }
        break;
    case ST_CA_DELETE:
        begin_ca_upload();
        break;
    case ST_CA_UPLOAD:
        g.handle_valid = 0U;
        g.state = ST_CA_OPEN;
        (void)submit_command("AT+QFOPEN=\"ota_ca.pem\",2", 5000U);
        break;
    case ST_MQTT_DISC:
        g.state = ST_MQTT_CLOSE;
        (void)snprintf(command, sizeof(command), "AT+QMTCLOSE=%u",
                       (unsigned)g.mqtt_client_index);
        (void)submit_command(command, 10000U);
        break;
    case ST_MQTT_CLOSE:
        if (g.seeding) {
            g.state = ST_FILE_DELETE;
            (void)snprintf(command, sizeof(command), "AT+QFDEL=\"%s\"",
                           slot_name(g.slot));
            (void)submit_command(command, 5000U);
        } else {
            start_common_downloader();
        }
        break;
    case ST_HTTP_STOP:
        g.state = ST_PDP_DEACT;
        (void)submit_command("AT+QIDEACT=1", 40000U);
        break;
    case ST_PDP_DEACT:
        g.state = ST_PDP_ACT;
        (void)submit_command("AT+QIACT=1", 150000U);
        break;
    case ST_PDP_ACT:
        g.state = ST_TLS_VERSION;
        (void)submit_command("AT+QSSLCFG=\"sslversion\",1,3", 5000U);
        break;
    case ST_TLS_VERSION:
        g.state = ST_TLS_LEVEL;
        break;
    case ST_TLS_LEVEL:
        g.state = ST_TLS_SNI;
        break;
    case ST_TLS_SNI:
        g.state = ST_TLS_CA;
        break;
    case ST_TLS_CA:
        g.state = ST_HTTP_CONTEXT;
        break;
    case ST_HTTP_CONTEXT:
        g.state = ST_HTTP_TLS;
        break;
    case ST_HTTP_TLS:
        g.state = ST_URL;
        break;
    case ST_URL:
        g.state = ST_FILE_DELETE;
        (void)snprintf(command, sizeof(command), "AT+QFDEL=\"%s\"", slot_name(g.slot));
        (void)submit_command(command, 5000U);
        break;
    case ST_FILE_DELETE:
        g.state = ST_FILE_OPEN;
        (void)snprintf(command, sizeof(command), "AT+QFOPEN=\"%s\",1", slot_name(g.slot));
        (void)submit_command(command, 5000U);
        break;
    case ST_FILE_OPEN:
        if (!g.handle_valid)
            download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
        else if (g.seeding) {
            g.chunk_size = g.expected_size - g.downloaded;
            if (g.chunk_size > ABOX_BOOT_V2_APP_RAW_CHUNK)
                g.chunk_size = ABOX_BOOT_V2_APP_RAW_CHUNK;
            g.state = ST_FILE_WRITE;
            (void)snprintf(command, sizeof(command), "AT+QFWRITE=%lu,%lu,10",
                           (unsigned long)g.handle, (unsigned long)g.chunk_size);
            (void)submit_command(command, 15000U);
        } else
            g.state = ST_RANGE_GET;
        break;
    case ST_RANGE_GET:
        if (g.http_status != 206U || g.http_length != g.chunk_size)
            download_fail(ABOX_BOOT_V2_APP_ERROR_HTTP);
        else {
            g.chunk_received = 0U;
            g.state = ST_HTTP_READ;
            (void)submit_command("AT+QHTTPREAD=80", 90000U);
        }
        break;
    case ST_HTTP_READ:
        if (g.chunk_received != g.chunk_size)
            download_fail(ABOX_BOOT_V2_APP_ERROR_SIZE);
        else {
            g.state = ST_FILE_WRITE;
            (void)snprintf(command, sizeof(command), "AT+QFWRITE=%lu,%lu,10",
                           (unsigned long)g.handle, (unsigned long)g.chunk_size);
            (void)submit_command(command, 15000U);
        }
        break;
    case ST_FILE_WRITE:
        if (g.written != g.chunk_size || g.written_total != g.downloaded + g.chunk_size)
            download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
        else {
            g.downloaded += g.chunk_size;
            if (g.downloaded == g.expected_size) {
                g.state = ST_FILE_CLOSE;
                (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu",
                               (unsigned long)g.handle);
                (void)submit_command(command, 5000U);
            } else {
                if (g.seeding) {
                    g.chunk_size = g.expected_size - g.downloaded;
                    if (g.chunk_size > ABOX_BOOT_V2_APP_RAW_CHUNK)
                        g.chunk_size = ABOX_BOOT_V2_APP_RAW_CHUNK;
                    (void)snprintf(command, sizeof(command),
                                   "AT+QFWRITE=%lu,%lu,10",
                                   (unsigned long)g.handle,
                                   (unsigned long)g.chunk_size);
                    (void)submit_command(command, 15000U);
                } else {
                    g.state = ST_RANGE_GET;
                }
            }
        }
        break;
    case ST_FILE_CLOSE:
        g.handle_valid = 0U;
        g.state = ST_FILE_LIST;
        (void)snprintf(command, sizeof(command), "AT+QFLST=\"%s\"", slot_name(g.slot));
        (void)submit_command(command, 5000U);
        break;
    case ST_FILE_LIST:
        if (!g.file_seen || g.file_size != g.expected_size)
            download_fail(ABOX_BOOT_V2_APP_ERROR_SIZE);
        else {
            g.state = ST_VERIFY_OPEN;
            (void)snprintf(command, sizeof(command), "AT+QFOPEN=\"%s\",0", slot_name(g.slot));
            (void)submit_command(command, 5000U);
        }
        break;
    case ST_VERIFY_OPEN:
        if (!g.handle_valid)
            download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
        else
            g.state = ST_VERIFY_READ;
        break;
    case ST_VERIFY_READ:
        if (g.read_size == g.expected_size) {
            g.state = ST_VERIFY_CLOSE;
            (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu",
                           (unsigned long)g.handle);
            (void)submit_command(command, 5000U);
        }
        break;
    case ST_VERIFY_CLOSE:
        g.handle_valid = 0U;
        g.state = ST_VERIFY;
        break;
    default:
        break;
    }
}

static void command_done(ABoxBootV2AtResult result, void *user)
{
    char command[64];
    (void)user;
    if (!g_initialized) return;

    if (result == ABOX_BOOT_V2_AT_OK) {
        command_succeeded();
        return;
    }

    if (g.state == ST_PROBE) {
        if (g.probe_index < (uint8_t)(sizeof(probes) / sizeof(probes[0])) &&
            strcmp(probes[g.probe_index], "AT+QHTTPREADFILE=?") == 0) {
            g.direct_supported = 0U;
            if (++g.probe_index < (uint8_t)(sizeof(probes) / sizeof(probes[0]))) {
                (void)submit_command(probes[g.probe_index], 5000U);
                return;
            }
        }
        provision_fail(ABOX_BOOT_V2_APP_ERROR_UNSUPPORTED);
        return;
    }
    if (g.state == ST_CA_OPEN && result == ABOX_BOOT_V2_AT_ERROR) {
        g.handle_valid = 0U;
        g.ca_repair_pending = 1U;
        g.state = ST_CA_QUERY;
        (void)submit_command("AT+QFOPEN?", 5000U);
        return;
    }
    if (g.state == ST_CA_READ && result == ABOX_BOOT_V2_AT_ERROR && g.handle_valid) {
        g.ca_repair_pending = 1U;
        g.state = ST_CA_CLOSE;
        (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu", (unsigned long)g.handle);
        (void)submit_command(command, 5000U);
        return;
    }
    if (g.state == ST_CA_DELETE && result == ABOX_BOOT_V2_AT_ERROR) {
        command_succeeded();
        return;
    }
    /* MQTT teardown is idempotent for local seeding and candidate downloads:
     * EC800 returns ERROR when the session is already disconnected or closed.
     * Timeouts and send failures still abort before touching the slot. */
    if (result == ABOX_BOOT_V2_AT_ERROR &&
        (g.state == ST_MQTT_DISC || g.state == ST_MQTT_CLOSE)) {
        command_succeeded();
        return;
    }
    if (provision_state(g.state)) {
        provision_fail(ABOX_BOOT_V2_APP_ERROR_CA);
        return;
    }
    if ((g.state == ST_FILE_DELETE || g.state == ST_HTTP_STOP) &&
        result == ABOX_BOOT_V2_AT_ERROR) {
        command_succeeded();
        return;
    }
    download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
}

static void event_received(ABoxBootV2AtEvent event, const uint8_t *data,
                           uint16_t length, void *user)
{
    char line[96];
    (void)user;
    if (!g_initialized) return;

    if (event == ABOX_BOOT_V2_AT_LINE && length == 3U && data &&
        memcmp(data, "RDY", 3U) == 0) {
        ABoxBootV2App_OnModemReset();
        return;
    }

    if (g.state == ST_DOWNLOADER) {
        ABoxHttpsUfs_OnEvent(&g.downloader, (ABoxHttpsUfsAtEvent)event,
                             data, length);
        return;
    }

    if (event == ABOX_BOOT_V2_AT_RAW) {
        if (g.state == ST_CA_READ) {
            if (g.ca_read_size + length > g.cfg.ca_pem_length) {
                provision_fail(ABOX_BOOT_V2_APP_ERROR_CA);
                return;
            }
            g.ca_crc = crc_update(g.ca_crc, data, length);
            g.ca_read_size += length;
        } else if (g.state == ST_HTTP_READ) {
            if (g.chunk_received + length > g.chunk_size ||
                g.chunk_received + length > g.port.transfer_buffer_size) {
                download_fail(ABOX_BOOT_V2_APP_ERROR_SIZE);
                return;
            }
            memcpy(g.port.transfer_buffer + g.chunk_received, data, length);
            g.chunk_received += length;
        } else if (g.state == ST_VERIFY_READ) {
            uint16_t copy = length;
            if (g.read_size + length > g.expected_size) {
                download_fail(ABOX_BOOT_V2_APP_ERROR_SIZE);
                return;
            }
            if (g.vector_len < sizeof(g.vector)) {
                if (copy > sizeof(g.vector) - g.vector_len)
                    copy = (uint16_t)(sizeof(g.vector) - g.vector_len);
                memcpy(g.vector + g.vector_len, data, copy);
                g.vector_len = (uint8_t)(g.vector_len + copy);
            }
            g.crc = crc_update(g.crc, data, length);
            g.read_size += length;
        }
        return;
    }

    if (length >= sizeof(line)) length = sizeof(line) - 1U;
    memcpy(line, data, length);
    line[length] = '\0';

    if (strncmp(line, "+QFOPEN:", 8U) == 0) {
        unsigned long handle;
        if (g.state == ST_CA_QUERY) {
            char name[64];
            if (sscanf(line, "+QFOPEN: \"%63[^\"]\",%lu", name, &handle) == 2 &&
                ufs_name_matches(name, ABOX_BOOT_V2_CA_FILE)) {
                g.handle = (uint32_t)handle;
                g.handle_valid = 1U;
            }
        } else if (sscanf(line, "+QFOPEN: %lu", &handle) == 1) {
            g.handle = (uint32_t)handle;
            g.handle_valid = 1U;
        }
    } else if (strncmp(line, "+QHTTPGET:", 10U) == 0) {
        unsigned long result;
        unsigned long status;
        unsigned long response_length;
        if (sscanf(line, "+QHTTPGET: %lu,%lu,%lu", &result, &status, &response_length) == 3 &&
            result == 0U) {
            g.http_status = (uint32_t)status;
            g.http_length = (uint32_t)response_length;
        }
    } else if (strncmp(line, "+QFWRITE:", 9U) == 0) {
        unsigned long written;
        unsigned long total;
        if (sscanf(line, "+QFWRITE: %lu,%lu", &written, &total) == 2) {
            g.written = (uint32_t)written;
            g.written_total = (uint32_t)total;
        }
    } else if (strncmp(line, "+QFLST:", 7U) == 0) {
        char name[32];
        unsigned long size;
        if (sscanf(line, "+QFLST: \"%31[^\"]\",%lu", name, &size) == 2 &&
            ufs_name_matches(name, slot_name(g.slot))) {
            g.file_seen = 1U;
            g.file_size = (uint32_t)size;
        }
    }

    if (strncmp(line, "CONNECT ", 8U) == 0 &&
        (g.state == ST_CA_READ || g.state == ST_VERIFY_READ)) {
        unsigned long raw_length;
        uint32_t remaining = g.state == ST_CA_READ
                                 ? g.cfg.ca_pem_length - g.ca_read_size
                                 : g.expected_size - g.read_size;
        if (sscanf(line, "CONNECT %lu", &raw_length) != 1 || raw_length == 0U ||
            raw_length > ABOX_BOOT_V2_APP_RAW_CHUNK || raw_length > remaining ||
            !g.port.begin_raw_read ||
            !g.port.begin_raw_read(g.port.context, (uint32_t)raw_length)) {
            if (g.state == ST_CA_READ)
                provision_fail(ABOX_BOOT_V2_APP_ERROR_CA);
            else
                download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
        }
    } else if (strcmp(line, "CONNECT") == 0 && g.state == ST_HTTP_READ) {
        if (!g.port.begin_raw_read ||
            !g.port.begin_raw_read(g.port.context, g.chunk_size))
            download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
    }
}

int ABoxBootV2App_Init(const ABoxBootV2AppPort *port,
                       const ABoxBootV2AppConfig *config)
{
    ABoxHttpsUfsPort downloader_port;
    ABoxHttpsUfsConfig downloader_config;
    /* Failed initialization must also invalidate an earlier idle binding. */
    g_initialized = 0U;
    if (!port || !config || !port->submit || !port->register_events ||
        !port->mqtt_client_index ||
        !port->transfer_buffer ||
        port->transfer_buffer_size < ABOX_BOOT_V2_APP_RAW_CHUNK ||
        (port->mqtt_stop && (!port->workspace_borrow ||
                             !port->workspace_return)) ||
        (!port->mqtt_stop && (port->workspace_borrow ||
                              port->workspace_return)) || !valid_config(config))
        return 0;

    memset(&g, 0, sizeof(g));
    g.port = *port;
    g.cfg = *config;
    g.direct_supported = 1U;
    g.state = ST_WAIT;
    memset(&downloader_port, 0, sizeof(downloader_port));
    downloader_port.tick_ms = downloader_tick;
    downloader_port.submit = downloader_submit;
    downloader_port.send_payload = downloader_send;
    downloader_port.begin_raw = downloader_raw;
    downloader_port.has_pending = downloader_pending;
    downloader_port.is_active = downloader_active;
    downloader_port.cancel = downloader_cancel;
    downloader_port.log = downloader_log;
    downloader_port.rx_overflow_count = downloader_overflow;
    memset(&downloader_config, 0, sizeof(downloader_config));
    downloader_config.ca_file = "UFS:" ABOX_BOOT_V2_CA_FILE;
    downloader_config.transfer_buffer = g.port.transfer_buffer;
    downloader_config.transfer_buffer_size = g.port.transfer_buffer_size;
    downloader_config.vector_valid = downloader_vector;
    downloader_config.direct_supported = 1U;
    if (!ABoxHttpsUfs_Init(&g.downloader, &downloader_port,
                           &downloader_config))
        return 0;
    if (!port->register_events(port->context, event_received, 0)) return 0;
    g_initialized = 1U;
    return 1;
}

int ABoxBootV2App_IsInitialized(void)
{
    return g_initialized;
}

void ABoxBootV2App_OnModemReset(void)
{
    if (!g_initialized) return;
    ABoxHttpsUfs_OnModemReset(&g.downloader);
    if (g.busy) download_fail(ABOX_BOOT_V2_APP_ERROR_UFS);
    else if (g.port.cancel) g.port.cancel(g.port.context);
    g.provisioned = g.handle_valid = g.payload_sent = 0U;
    g.provision_retry_count = 0U;
    g.stable_checked = g.seed_attempted = 0U;
    g.state = ST_RETRY;
    g.retry_due = now() + ABOX_BOOT_V2_PROVISION_RETRY_1_MS;
}

int ABoxBootV2App_BeginProvisioning(void)
{
    if (!g_initialized || g.busy) return 0;
    start_provisioning();
    return g.state == ST_PROBE || g.state == ST_RETRY || g.state == ST_ERROR;
}

int ABoxBootV2App_Start(const ABoxBootV2AppRequest *request)
{
    if (!g_initialized) return 0;
    return begin_request(request, 0U);
}

static int stable_slot_needs_seed(void)
{
    ABoxBootV2Record state;
    uint32_t running_crc;
    if (!ABoxBootV2_StateLoad(&state)) return 1;
    if (state.state != ABOX_BOOT_V2_CONFIRMED && state.state != ABOX_BOOT_V2_NORMAL)
        return 0;
    if (!running_image_crc(&running_crc)) return 1;
    return state.stable_image_size != g.cfg.app_size ||
           state.stable_image_crc32 != running_crc ||
           strncmp(state.stable_image_version, g.cfg.running_version,
                   sizeof(state.stable_image_version)) != 0;
}

static int stable_slot_check_eligible(void)
{
    ABoxBootV2Record state;
    if (!ABoxBootV2_StateLoad(&state)) return 1;
    return state.state == ABOX_BOOT_V2_CONFIRMED ||
           state.state == ABOX_BOOT_V2_NORMAL;
}

static void commit_verified_candidate(void)
{
    ABoxBootV2Record state;
    const uint8_t was_seeding = g.seeding;
    memset(&state, 0, sizeof(state));
    (void)ABoxBootV2_StateLoad(&state);
    state.candidate_slot = g.slot;
    state.image_size = g.expected_size;
    state.image_crc32 = g.expected_crc;
    (void)snprintf(state.image_version, sizeof(state.image_version), "%s",
                   g.version);
    if (was_seeding) {
        state.state = ABOX_BOOT_V2_CONFIRMED;
        state.stable_slot = g.slot;
        state.stable_image_size = g.expected_size;
        state.stable_image_crc32 = g.expected_crc;
        (void)snprintf(state.stable_image_version,
                       sizeof(state.stable_image_version), "%s", g.version);
    } else {
        state.state = ABOX_BOOT_V2_INSTALL_PENDING;
    }
    if (!ABoxBootV2_StateSave(&state)) {
        download_fail(ABOX_BOOT_V2_APP_ERROR_STATE);
        return;
    }
    g.busy = 0U;
    g.seeding = 0U;
    g.install_ready = (uint8_t)!was_seeding;
    g.last_error = 0U;
    g.state = ST_IDLE;
    resume_mqtt();
    log_message(1U, was_seeding ? "Boot V2 stable slot seeded"
                                 : "Boot V2 candidate verified");
}

void ABoxBootV2App_Task(void)
{
    char command[128];
    ABoxBootV2Record state;
    uint32_t downloader_error;

    if (!g_initialized) return;

    if ((g.state == ST_ERROR || g.state == ST_RETRY) &&
        (int32_t)(now() - g.retry_due) >= 0) {
        if (g.state == ST_ERROR) g.provision_retry_count = 0U;
        start_provisioning();
        return;
    }
    if (g.state == ST_ERROR || g.state == ST_RETRY) return;

    if (g.state == ST_MQTT_QUIESCE) {
        int stopped = g.port.mqtt_stop(g.port.context, now());
        if (stopped < 0) {
            download_fail(ABOX_BOOT_V2_APP_ERROR_STATE);
            return;
        }
        if (!stopped) return;
        if (!g.port.workspace_borrow(g.port.context)) {
            download_fail(ABOX_BOOT_V2_APP_ERROR_STATE);
            return;
        }
        g.workspace_borrowed = 1U;
        g.state = ST_MQTT_CLOSE;
        command_succeeded();
        return;
    }

    if (g.state == ST_DOWNLOADER) {
        ABoxHttpsUfs_Task(&g.downloader);
        if (ABoxHttpsUfs_TakeSuccess(&g.downloader))
            commit_verified_candidate();
        else if (ABoxHttpsUfs_TakeFailure(&g.downloader, &downloader_error))
            download_fail(downloader_error);
        return;
    }

    if (g.provisioned && !g.busy && !g.seed_attempted && !g.stable_checked &&
        stable_slot_check_eligible()) {
        g.stable_checked = 1U;
        if (stable_slot_needs_seed()) {
            g.seed_attempted = 1U;
            if (!begin_local_seed()) {
                g.seed_attempted = 0U;
                g.stable_checked = 0U;
                download_fail(ABOX_BOOT_V2_APP_ERROR_STATE);
            }
        }
        return;
    }

    if (g.port.is_active && g.port.is_active(g.port.context) && !g.payload_sent) {
        if (g.state == ST_CA_UPLOAD) {
            g.payload_sent = (uint8_t)g.port.send_payload(
                g.port.context, g.cfg.ca_pem, (uint16_t)g.cfg.ca_pem_length);
            return;
        }
        if (g.state == ST_URL) {
            g.payload_sent = (uint8_t)g.port.send_payload(
                g.port.context, (const uint8_t *)g.url, (uint16_t)strlen(g.url));
            return;
        }
        if (g.state == ST_FILE_WRITE) {
            const uint8_t *payload = g.port.transfer_buffer;
            if (g.seeding &&
                !ABox_PortFlashRead(g.cfg.app_start_addr + g.downloaded,
                                    g.port.transfer_buffer, g.chunk_size)) {
                download_fail(ABOX_BOOT_V2_APP_ERROR_STATE);
                return;
            }
            g.payload_sent = (uint8_t)g.port.send_payload(
                g.port.context, payload, (uint16_t)g.chunk_size);
            return;
        }
    }

    if (g.port.has_pending && g.port.has_pending(g.port.context)) return;

    switch (g.state) {
    case ST_TLS_LEVEL:
        (void)submit_command("AT+QSSLCFG=\"seclevel\",1,1", 5000U);
        break;
    case ST_TLS_SNI:
        (void)submit_command("AT+QSSLCFG=\"sni\",1,1", 5000U);
        break;
    case ST_TLS_CA:
        (void)submit_command("AT+QSSLCFG=\"cacert\",1,\"UFS:ota_ca.pem\"", 5000U);
        break;
    case ST_HTTP_CONTEXT:
        (void)submit_command("AT+QHTTPCFG=\"contextid\",1", 5000U);
        break;
    case ST_HTTP_TLS:
        (void)submit_command("AT+QHTTPCFG=\"sslctxid\",1", 5000U);
        break;
    case ST_URL:
        (void)snprintf(command, sizeof(command), "AT+QHTTPURL=%u,80",
                       (unsigned)strlen(g.url));
        (void)submit_command(command, 10000U);
        break;
    case ST_RANGE_GET:
        g.chunk_size = g.expected_size - g.downloaded;
        if (g.chunk_size > ABOX_BOOT_V2_APP_RAW_CHUNK)
            g.chunk_size = ABOX_BOOT_V2_APP_RAW_CHUNK;
        g.http_status = 0U;
        g.http_length = 0U;
        (void)snprintf(command, sizeof(command), "AT+QHTTPGETEX=80,%lu,%lu",
                       (unsigned long)g.downloaded, (unsigned long)g.chunk_size);
        (void)submit_command(command, 90000U);
        break;
    case ST_VERIFY_READ: {
        uint32_t length = g.expected_size - g.read_size;
        if (length > ABOX_BOOT_V2_APP_RAW_CHUNK) length = ABOX_BOOT_V2_APP_RAW_CHUNK;
        (void)snprintf(command, sizeof(command), "AT+QFREAD=%lu,%lu",
                       (unsigned long)g.handle, (unsigned long)length);
        (void)submit_command(command, 10000U);
        break;
    }
    case ST_VERIFY:
        g.crc ^= 0xFFFFFFFFU;
        if (g.read_size != g.expected_size)
            download_fail(ABOX_BOOT_V2_APP_ERROR_SIZE);
        else if (g.crc != g.expected_crc)
            download_fail(ABOX_BOOT_V2_APP_ERROR_CRC);
        else if (!ABoxBootV2_ImageVectorValid(g.vector))
            download_fail(ABOX_BOOT_V2_APP_ERROR_VECTOR);
        else {
            memset(&state, 0, sizeof(state));
            (void)ABoxBootV2_StateLoad(&state);
            state.candidate_slot = g.slot;
            state.image_size = g.expected_size;
            state.image_crc32 = g.expected_crc;
            (void)snprintf(state.image_version, sizeof(state.image_version), "%s", g.version);
            if (g.seeding) {
                state.state = ABOX_BOOT_V2_CONFIRMED;
                state.stable_slot = g.slot;
                state.stable_image_size = g.expected_size;
                state.stable_image_crc32 = g.expected_crc;
                (void)snprintf(state.stable_image_version,
                               sizeof(state.stable_image_version), "%s", g.version);
            } else {
                state.state = ABOX_BOOT_V2_INSTALL_PENDING;
            }
            if (!ABoxBootV2_StateSave(&state))
                download_fail(ABOX_BOOT_V2_APP_ERROR_STATE);
            else {
                const uint8_t was_seeding = g.seeding;
                g.busy = 0U;
                g.seeding = 0U;
                g.install_ready = (uint8_t)!was_seeding;
                g.last_error = 0U;
                g.state = ST_IDLE;
                resume_mqtt();
                log_message(1U, was_seeding ? "Boot V2 stable slot seeded"
                                             : "Boot V2 candidate verified");
            }
        }
        break;
    default:
        break;
    }
}

int ABoxBootV2App_IsProvisioned(void)
{
    return g_initialized && g.provisioned;
}

int ABoxBootV2App_IsReady(void)
{
    ABoxBootV2Record state;
    ABoxBootV2Descriptor descriptor;
    return g_initialized && g.provisioned && ABoxBootV2_DescriptorRead(&descriptor) &&
           ABoxBootV2_DescriptorReady(&descriptor) && ABoxBootV2_StateLoad(&state) &&
           state.stable_slot <= 1U && state.stable_image_size != 0U;
}

int ABoxBootV2App_IsBusy(void)
{
    return g_initialized && g.busy;
}

uint32_t ABoxBootV2App_LastError(void)
{
    return g_initialized ? g.last_error : ABOX_BOOT_V2_APP_ERROR_STATE;
}

const char *ABoxBootV2App_TargetVersion(void)
{
    return g_initialized ? g.version : "";
}

const char *ABoxBootV2App_Phase(void)
{
    if (!g_initialized) return "INIT_FAILED";
    if (g.busy && g.seeding) return "SEEDING_LOCAL";
    if (g.state == ST_WAIT) return "WAIT_MODEM";
    if (g.state == ST_RETRY) return "RETRY_WAIT";
    if (provision_state(g.state)) return "PROVISIONING";
    if (g.state == ST_DOWNLOADER) return ABoxHttpsUfs_Phase(&g.downloader);
    if (g.state >= ST_MQTT_DISC && g.state <= ST_FILE_CLOSE) return "DOWNLOADING";
    if (g.state >= ST_FILE_LIST && g.state <= ST_VERIFY) return "VERIFYING";
    if (g.state == ST_ERROR) return "ERROR";
    return "IDLE";
}

const ABoxHttpsUfsMetrics *ABoxBootV2App_DownloadMetrics(void)
{
    return g_initialized ? ABoxHttpsUfs_Metrics(&g.downloader) : NULL;
}

const char *ABoxBootV2App_StableSeedSource(void)
{
    return g_abox_boot_v2_stable_seed_source_marker + sizeof("ABOX_STABLE_SEED_SOURCE=") - 1U;
}

int ABoxBootV2App_TakeInstallReady(void)
{
    if (!g_initialized) return 0;
    int value = g.install_ready;
    g.install_ready = 0U;
    return value;
}

int ABoxBootV2App_TakeFailure(uint32_t *error)
{
    if (!g_initialized) {
        if (error) *error = ABOX_BOOT_V2_APP_ERROR_STATE;
        return 0;
    }
    int value = g.failure_pending;
    g.failure_pending = 0U;
    if (error) *error = g.last_error;
    return value;
}
