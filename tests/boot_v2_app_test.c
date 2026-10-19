#include "abox_boot_v2_app.h"
#include "abox_platform_port.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(1); \
} } while (0)
#define assert(condition) CHECK(condition)

#define BASE 0x08000000U
#define STATE_A 0x0803E800U
#define STATE_B 0x0803F000U
#define PROBE_COUNT 15U

static uint8_t flash_mem[0x40000];
static uint8_t transfer[ABOX_BOOT_V2_APP_RAW_CHUNK];
static ABoxBootV2AtDoneFn pending_done;
static void *pending_user;
static ABoxBootV2AtEventFn event_fn;
static void *event_user;
static char command[300];
static char last_log[160];
static uint8_t pending;
static uint8_t active;
static uint8_t payload_allowed;
static uint8_t mqtt_paused;
static uint32_t payload_count;
static uint8_t sent_payload[2048];
static uint32_t sent_payload_length;
static uint32_t raw_wanted;
static uint32_t tick_now;
static uint32_t flash_read_count;

static const uint8_t ca[] = "test-ca";
static const ABoxBootV2Layout boot_layout = {
    STATE_A, STATE_B, 0x800U, 0x08008000U, 0x0803E7FFU, 0x20000000U, 0x2000FFFFU
};
static const ABoxFlashLayout legacy_layout = {0x08008000U, 0x0803F800U, 0x800U};

static int flash_begin(void *context) { (void)context; return 1; }
static void flash_end(void *context) { (void)context; }
static int flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length)
{
    (void)context;
    if (address < BASE || address - BASE + length > sizeof(flash_mem)) return 0;
    ++flash_read_count;
    memcpy(data, flash_mem + address - BASE, length);
    return 1;
}
static int flash_write(void *context, uint32_t address, const uint8_t *data, uint32_t length)
{
    (void)context;
    if (address < BASE || address - BASE + length > sizeof(flash_mem)) return 0;
    memcpy(flash_mem + address - BASE, data, length);
    return 1;
}
static int flash_erase(void *context, uint32_t address)
{
    (void)context;
    if (address < BASE || address - BASE + 0x800U > sizeof(flash_mem)) return 0;
    memset(flash_mem + address - BASE, 0xFF, 0x800U);
    return 1;
}
static uint32_t tick(void *context) { (void)context; return tick_now; }
static int uart(void *context, const uint8_t *data, uint32_t length)
{ (void)context; (void)data; return length != 0U; }
static void critical(void *context) { (void)context; }

static int submit(void *context, const char *value, uint32_t timeout,
                  ABoxBootV2AtDoneFn done, void *user)
{
    (void)context;
    (void)timeout;
    assert(!pending);
    snprintf(command, sizeof(command), "%s", value);
    pending_done = done;
    pending_user = user;
    pending = 1U;
    active = 1U;
    payload_allowed = 0U;
    return 1;
}
static int send_payload(void *context, const uint8_t *data, uint16_t length)
{
    (void)context;
    assert(data && length);
    if (!payload_allowed) return 0;
    payload_allowed = 0U;
    ++payload_count;
    assert(sent_payload_length + length <= sizeof(sent_payload));
    memcpy(sent_payload + sent_payload_length, data, length);
    sent_payload_length += length;
    return 1;
}
static int begin_raw(void *context, uint32_t length)
{ (void)context; raw_wanted = length; return 1; }
static int has_pending(void *context) { (void)context; return pending; }
static int is_active(void *context) { (void)context; return active; }
static void cancel(void *context)
{
    (void)context;
    pending = 0U;
    active = 0U;
    pending_done = 0;
    pending_user = 0;
}
static void register_events(void *context, ABoxBootV2AtEventFn callback, void *user)
{ (void)context; event_fn = callback; event_user = user; }
static void mqtt_pause(void *context, uint8_t paused) { (void)context; mqtt_paused = paused; }
static void log_line(void *context, uint8_t level, const char *message)
{
    (void)context;
    (void)level;
    snprintf(last_log, sizeof(last_log), "%s", message ? message : "");
}

static void complete(ABoxBootV2AtResult result)
{
    ABoxBootV2AtDoneFn callback = pending_done;
    void *user = pending_user;
    assert(pending && callback);
    pending = 0U;
    active = 0U;
    pending_done = 0;
    pending_user = 0;
    callback(result, user);
}

static void line(const char *value)
{
    assert(event_fn);
    if (strcmp(value, "CONNECT") == 0 || strncmp(value, "CONNECT ", 8U) == 0)
        payload_allowed = 1U;
    event_fn(ABOX_BOOT_V2_AT_LINE, (const uint8_t *)value,
             (uint16_t)strlen(value), event_user);
}

static void raw(const uint8_t *data, uint16_t length)
{
    assert(event_fn);
    event_fn(ABOX_BOOT_V2_AT_RAW, data, length, event_user);
}

static void reset_transport(void)
{
    pending_done = 0;
    pending_user = 0;
    event_fn = 0;
    event_user = 0;
    command[0] = '\0';
    last_log[0] = '\0';
    pending = 0U;
    active = 0U;
    payload_allowed = 0U;
    mqtt_paused = 0U;
    payload_count = 0U;
    sent_payload_length = 0U;
    raw_wanted = 0U;
    tick_now = 1000U;
    flash_read_count = 0U;
}

static void bind_platform(void)
{
    static const ABoxPlatformPort platform = {
        0, tick, uart, flash_begin, flash_write, flash_erase, flash_end,
        critical, critical, 0, 0, &legacy_layout, 0, 0, flash_read
    };
    assert(ABox_PlatformPortBind(&platform));
    assert(ABoxBootV2_StateBindLayout(&boot_layout));
}

static void prepare_flash(void)
{
    ABoxBootV2Record state;
    ABoxBootV2Descriptor descriptor;

    memset(flash_mem, 0xFF, sizeof(flash_mem));
    memset(&state, 0, sizeof(state));
    state.state = ABOX_BOOT_V2_CONFIRMED;
    state.stable_slot = 0U;
    state.stable_image_size = 64U;
    state.stable_image_crc32 = 0x11111111U;
    memcpy(state.stable_image_version, "abox-v2-stable", 15U);
    assert(ABoxBootV2_StateSave(&state));

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.magic = ABOX_BOOT_V2_DESCRIPTOR_MAGIC;
    descriptor.abi_version = 1U;
    descriptor.length = sizeof(descriptor);
    descriptor.feature_flags = ABOX_BOOT_V2_REQUIRED_FEATURES;
    descriptor.crc32 = ABoxBootV2_Crc32(&descriptor,
                                         (uint32_t)offsetof(ABoxBootV2Descriptor, crc32));
    memcpy(flash_mem + ABOX_BOOT_V2_DESCRIPTOR_ADDR - BASE, &descriptor, sizeof(descriptor));
}

static void init_app(void)
{
    ABoxBootV2AppPort port = {
        0, tick, submit, send_payload, begin_raw, has_pending, is_active,
        cancel, register_events, mqtt_pause, log_line, transfer, sizeof(transfer)
    };
    ABoxBootV2AppConfig config = {
        "https://ota.example:20443", "meal_delivery_vehicle", "meal_delivery_vehicle_app.bin",
        "abox-v2-", "abox-v2-current", ca, sizeof(ca) - 1U,
        0x08008000U, 128U, 0x36800U
    };
    assert(ABoxBootV2App_Init(&port, &config));
}

static void complete_probes(void)
{
    uint32_t index;
    assert(ABoxBootV2App_BeginProvisioning());
    for (index = 0U; index < PROBE_COUNT; ++index) complete(ABOX_BOOT_V2_AT_OK);
    assert(strstr(command, "QFOPEN=\"ota_ca.pem\",2") != 0);
}

static void finish_existing_ca(void)
{
    line("+QFOPEN: 4");
    complete(ABOX_BOOT_V2_AT_OK);
    assert(strstr(command, "QFREAD=4,7") != 0);
    line("CONNECT 7");
    assert(raw_wanted == sizeof(ca) - 1U);
    raw(ca, sizeof(ca) - 1U);
    complete(ABOX_BOOT_V2_AT_OK);
    assert(strstr(command, "QFCLOSE=4") != 0);
    complete(ABOX_BOOT_V2_AT_OK);
    assert(ABoxBootV2App_IsProvisioned());
    assert(ABoxBootV2App_LastError() == 0U);
}

static void test_probe_error_and_retry(void)
{
    reset_transport();
    init_app();
    assert(ABoxBootV2App_BeginProvisioning());
    complete(ABOX_BOOT_V2_AT_ERROR);
    assert(ABoxBootV2App_LastError() == ABOX_BOOT_V2_APP_ERROR_UNSUPPORTED);
    assert(strcmp(ABoxBootV2App_Phase(), "RETRY_WAIT") == 0);
    assert(strstr(last_log, "AT+QFLDS") != 0);
    tick_now += 4999U;
    ABoxBootV2App_Task();
    assert(!pending);
    ++tick_now;
    ABoxBootV2App_Task();
    assert(pending);
    assert(strstr(command, "QFLDS") != 0);
}

static void test_missing_ca_is_uploaded_and_verified(void)
{
    reset_transport();
    init_app();
    complete_probes();
    complete(ABOX_BOOT_V2_AT_ERROR);
    assert(strcmp(command, "AT+QFOPEN?") == 0);
    complete(ABOX_BOOT_V2_AT_OK);
    assert(strstr(command, "QFUPL=\"ota_ca.pem\",7,30") != 0);
    ABoxBootV2App_Task();
    assert(payload_count == 0U);
    line("CONNECT");
    ABoxBootV2App_Task();
    assert(payload_count == 1U);
    complete(ABOX_BOOT_V2_AT_OK);
    assert(strstr(command, "QFOPEN=\"ota_ca.pem\",2") != 0);
    finish_existing_ca();
}

static void test_ca_error_is_distinct(void)
{
    reset_transport();
    init_app();
    complete_probes();
    complete(ABOX_BOOT_V2_AT_ERROR);
    complete(ABOX_BOOT_V2_AT_OK);
    assert(strstr(command, "QFUPL") != 0);
    complete(ABOX_BOOT_V2_AT_ERROR);
    assert(ABoxBootV2App_LastError() == ABOX_BOOT_V2_APP_ERROR_CA);
    assert(strcmp(ABoxBootV2App_Phase(), "RETRY_WAIT") == 0);
    assert(strstr(last_log, "CA_UPLOAD") != 0);
}

static void test_candidate_download(ABoxBootV2AtResult disconnect_result,
                                    ABoxBootV2AtResult close_result)
{
    ABoxBootV2AppRequest request;
    ABoxBootV2Record state;
    uint8_t image[128];

    prepare_flash();
    reset_transport();
    init_app();
    complete_probes();
    finish_existing_ca();

    memset(&request, 0, sizeof(request));
    snprintf(request.url, sizeof(request.url), "https://wrong.example/fw.bin");
    snprintf(request.version, sizeof(request.version), "abox-v2-next");
    request.size = sizeof(image);
    request.crc32 = 1U;
    assert(!ABoxBootV2App_Start(&request));
    assert(ABoxBootV2App_LastError() == ABOX_BOOT_V2_APP_ERROR_URL);

    memset(image, 0xA5, sizeof(image));
    image[0] = 0x00U; image[1] = 0x40U; image[2] = 0x00U; image[3] = 0x20U;
    image[4] = 0x01U; image[5] = 0x81U; image[6] = 0x00U; image[7] = 0x08U;
    snprintf(request.url, sizeof(request.url),
             "https://ota.example:20443/fw-revisions/meal_delivery_vehicle/abox-v2-next/meal_delivery_vehicle_app.bin");
    request.crc32 = ABoxBootV2_Crc32(image, sizeof(image));
    assert(ABoxBootV2App_Start(&request));
    assert(mqtt_paused);

    complete(disconnect_result); /* QMTDISC */
    assert(strstr(command, "QMTCLOSE") != 0);
    complete(close_result); /* QMTCLOSE */
    {
        uint32_t guard = 100U;
        while (!ABoxBootV2App_TakeInstallReady() && guard--) {
            char current[sizeof(command)];
            ABoxBootV2App_Task();
            if (!pending) {
                assert(ABoxBootV2App_TakeInstallReady());
                break;
            }
            assert(pending);
            snprintf(current, sizeof(current), "%s", command);
            if (strstr(current, "QHTTPURL") != 0) {
                line("CONNECT");
                ABoxBootV2App_Task();
            } else if (strcmp(current, "AT+QHTTPGET=80") == 0) {
                line("+QHTTPGET: 0,200,128");
            } else if (strstr(current, "QFLST") != 0) {
                line("+QFLST: \"UFS:fw_slot1.bin\",128");
            } else if (strstr(current, "QFOPEN=\"fw_slot1.bin\",2") != 0) {
                line("+QFOPEN: 8");
            } else if (strstr(current, "QFREAD=8,128") != 0) {
                line("CONNECT 128");
                assert(raw_wanted == sizeof(image));
                raw(image, sizeof(image));
            }
            if (strcmp(current, "AT+QHTTPSTOP") == 0 ||
                strcmp(current, "AT+QIDEACT=1") == 0 ||
                strstr(current, "QFDEL") != 0)
                complete(ABOX_BOOT_V2_AT_ERROR);
            else
                complete(ABOX_BOOT_V2_AT_OK);
        }
        assert(guard != 0U);
    }

    assert(!mqtt_paused);
    assert(ABoxBootV2_StateLoad(&state));
    assert(state.state == ABOX_BOOT_V2_INSTALL_PENDING);
    assert(state.candidate_slot == 1U);
    assert(state.image_size == sizeof(image));
    assert(state.image_crc32 == request.crc32);
    assert(strcmp(state.image_version, "abox-v2-next") == 0);
}

static void test_local_stable_seed_does_not_use_http(ABoxBootV2AtResult disconnect_result,
                                                    ABoxBootV2AtResult close_result)
{
    ABoxBootV2Record state;
    ABoxBootV2Descriptor descriptor;
    uint8_t image[128];

    memset(flash_mem, 0xFF, sizeof(flash_mem));
    memset(image, 0xA5, sizeof(image));
    image[0] = 0x00U; image[1] = 0x40U; image[2] = 0x00U; image[3] = 0x20U;
    image[4] = 0x01U; image[5] = 0x81U; image[6] = 0x00U; image[7] = 0x08U;
    memcpy(flash_mem + 0x8000U, image, sizeof(image));
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.magic = ABOX_BOOT_V2_DESCRIPTOR_MAGIC;
    descriptor.abi_version = ABOX_BOOT_V2_DESCRIPTOR_ABI;
    descriptor.length = sizeof(descriptor);
    descriptor.feature_flags = ABOX_BOOT_V2_REQUIRED_FEATURES;
    descriptor.crc32 = ABoxBootV2_Crc32(
        &descriptor, (uint32_t)offsetof(ABoxBootV2Descriptor, crc32));
    memcpy(flash_mem + ABOX_BOOT_V2_DESCRIPTOR_ADDR - BASE,
           &descriptor, sizeof(descriptor));
    reset_transport();
    init_app();
    complete_probes();
    finish_existing_ca();
    sent_payload_length = 0U;
    payload_count = 0U;

    ABoxBootV2App_Task();
    assert(strcmp(ABoxBootV2App_Phase(), "SEEDING_LOCAL") == 0);
    assert(strcmp(ABoxBootV2App_StableSeedSource(), "local_flash") == 0);
    assert(strstr(command, "QMTDISC") != 0);
    complete(disconnect_result);
    assert(strstr(command, "QMTCLOSE") != 0);
    complete(close_result);
    assert(strstr(command, "QFDEL=\"fw_slot0.bin\"") != 0);
    assert(strstr(command, "QHTTP") == 0);
    complete(ABOX_BOOT_V2_AT_ERROR);
    line("+QFOPEN: 7");
    complete(ABOX_BOOT_V2_AT_OK);
    assert(strstr(command, "QFWRITE=7,128,10") != 0);
    line("CONNECT");
    ABoxBootV2App_Task();
    assert(sent_payload_length == sizeof(image));
    assert(memcmp(sent_payload, image, sizeof(image)) == 0);
    line("+QFWRITE: 128,128");
    complete(ABOX_BOOT_V2_AT_OK);
    complete(ABOX_BOOT_V2_AT_OK);
    line("+QFLST: \"UFS:fw_slot0.bin\",128");
    complete(ABOX_BOOT_V2_AT_OK);
    line("+QFOPEN: 8");
    complete(ABOX_BOOT_V2_AT_OK);
    ABoxBootV2App_Task();
    line("CONNECT 128");
    raw(image, sizeof(image));
    complete(ABOX_BOOT_V2_AT_OK);
    complete(ABOX_BOOT_V2_AT_OK);
    ABoxBootV2App_Task();

    assert(!mqtt_paused);
    assert(ABoxBootV2App_IsReady());
    assert(ABoxBootV2_StateLoad(&state));
    assert(state.state == ABOX_BOOT_V2_CONFIRMED);
    assert(state.stable_slot == 0U);
    assert(state.stable_image_size == sizeof(image));
    assert(state.stable_image_crc32 == ABoxBootV2_Crc32(image, sizeof(image)));
    assert(strcmp(state.stable_image_version, "abox-v2-current") == 0);
}

static void test_matching_stable_slot_is_checked_only_once(void)
{
    ABoxBootV2Record state;
    ABoxBootV2Descriptor descriptor;
    uint8_t image[128];
    uint32_t reads_after_check;
    uint32_t index;

    memset(flash_mem, 0xFF, sizeof(flash_mem));
    memset(image, 0xA5, sizeof(image));
    image[0] = 0x00U; image[1] = 0x40U; image[2] = 0x00U; image[3] = 0x20U;
    image[4] = 0x01U; image[5] = 0x81U; image[6] = 0x00U; image[7] = 0x08U;
    memcpy(flash_mem + 0x8000U, image, sizeof(image));

    memset(&state, 0, sizeof(state));
    state.state = ABOX_BOOT_V2_CONFIRMED;
    state.stable_slot = 0U;
    state.stable_image_size = sizeof(image);
    state.stable_image_crc32 = ABoxBootV2_Crc32(image, sizeof(image));
    snprintf(state.stable_image_version, sizeof(state.stable_image_version),
             "abox-v2-current");
    assert(ABoxBootV2_StateSave(&state));

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.magic = ABOX_BOOT_V2_DESCRIPTOR_MAGIC;
    descriptor.abi_version = ABOX_BOOT_V2_DESCRIPTOR_ABI;
    descriptor.length = sizeof(descriptor);
    descriptor.feature_flags = ABOX_BOOT_V2_REQUIRED_FEATURES;
    descriptor.crc32 = ABoxBootV2_Crc32(
        &descriptor, (uint32_t)offsetof(ABoxBootV2Descriptor, crc32));
    memcpy(flash_mem + ABOX_BOOT_V2_DESCRIPTOR_ADDR - BASE,
           &descriptor, sizeof(descriptor));

    reset_transport();
    init_app();
    complete_probes();
    finish_existing_ca();

    flash_read_count = 0U;
    memset(transfer, 0x5AU, sizeof(transfer));
    ABoxBootV2App_Task();
    reads_after_check = flash_read_count;
    assert(reads_after_check > 0U);
    assert(strcmp(ABoxBootV2App_Phase(), "IDLE") == 0);

    memset(transfer, 0x5AU, sizeof(transfer));
    for (index = 0U; index < 5U; ++index) ABoxBootV2App_Task();
    assert(flash_read_count == reads_after_check);
    for (index = 0U; index < sizeof(transfer); ++index)
        assert(transfer[index] == 0x5AU);
}

static void test_download_failure_waits_before_retry(ABoxBootV2AtResult failure,
                                                     uint8_t fail_on_close)
{
    ABoxBootV2AppRequest request;
    ABoxBootV2Record state;

    prepare_flash();
    reset_transport();
    init_app();
    complete_probes();
    finish_existing_ca();

    memset(&request, 0, sizeof(request));
    snprintf(request.url, sizeof(request.url),
             "https://ota.example:20443/fw-revisions/meal_delivery_vehicle/abox-v2-next/meal_delivery_vehicle_app.bin");
    snprintf(request.version, sizeof(request.version), "abox-v2-next");
    request.size = 128U;
    request.crc32 = 1U;
    assert(ABoxBootV2App_Start(&request));
    if (fail_on_close) complete(ABOX_BOOT_V2_AT_OK);
    complete(failure);
    assert(ABoxBootV2App_LastError() == ABOX_BOOT_V2_APP_ERROR_UFS);
    assert(strstr(last_log, fail_on_close ? "MQTT_CLOSE" : "MQTT_DISC") != 0);
    assert(!mqtt_paused);
    assert(!ABoxBootV2App_IsBusy());
    assert(ABoxBootV2_StateLoad(&state));
    assert(state.state == ABOX_BOOT_V2_CONFIRMED);
    assert(!pending);
    ABoxBootV2App_Task();
    assert(!pending);
    tick_now += 299999U;
    ABoxBootV2App_Task();
    assert(!pending);
    ++tick_now;
    ABoxBootV2App_Task();
    assert(pending);
    assert(strstr(command, "QFLDS") != 0);
}

int main(void)
{
    bind_platform();
    prepare_flash();
    test_probe_error_and_retry();
    test_missing_ca_is_uploaded_and_verified();
    test_ca_error_is_distinct();
    test_local_stable_seed_does_not_use_http(ABOX_BOOT_V2_AT_OK, ABOX_BOOT_V2_AT_OK);
    test_local_stable_seed_does_not_use_http(ABOX_BOOT_V2_AT_ERROR, ABOX_BOOT_V2_AT_OK);
    test_local_stable_seed_does_not_use_http(ABOX_BOOT_V2_AT_OK, ABOX_BOOT_V2_AT_ERROR);
    test_local_stable_seed_does_not_use_http(ABOX_BOOT_V2_AT_ERROR, ABOX_BOOT_V2_AT_ERROR);
    test_matching_stable_slot_is_checked_only_once();
    prepare_flash();
    test_candidate_download(ABOX_BOOT_V2_AT_OK, ABOX_BOOT_V2_AT_OK);
    test_candidate_download(ABOX_BOOT_V2_AT_ERROR, ABOX_BOOT_V2_AT_ERROR);
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_TIMEOUT, 0U);
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_TIMEOUT, 1U);
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_SEND_FAIL, 0U);
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_SEND_FAIL, 1U);
    return 0;
}
