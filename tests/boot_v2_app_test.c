#include "abox_boot_v2_app.h"
#include "abox_platform_port.h"
#include "abox_boot_v2_installer.h"
#include "abox_boot_v2_nor.h"
#include "abox_mqtt_idem_nor.h"
#undef NDEBUG
#include "nor_test_chip.h"
#include <setjmp.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(1); \
} } while (0)
#undef assert
#define assert(condition) CHECK(condition)

#define BASE 0x08000000U
#define STATE_A 0x0803E800U
#define STATE_B 0x0803F000U
#define PROBE_COUNT 15U

static NorTestChip nor_chip;
static ABoxNor test_nor;
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
static uint8_t mqtt_index;
static uint32_t payload_count;
static uint8_t sent_payload[2048];
static uint32_t sent_payload_length;
static uint32_t raw_wanted;
static uint32_t tick_now;
static uint32_t flash_read_count;
static int delegated_stop_result;
static uint32_t delegated_stop_calls, delegated_borrows, delegated_returns;
static int use_seed_gate, seed_allowed;
static int may_start_seed(void *context) { (void)context; return seed_allowed; }

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
static int register_events(void *context, ABoxBootV2AtEventFn callback, void *user)
{ (void)context; event_fn = callback; event_user = user; return 1; }
static void mqtt_pause(void *context, uint8_t paused) { (void)context; mqtt_paused = paused; }
static int delegated_stop(void *context, uint32_t now)
{
    (void)context;
    (void)now;
    ++delegated_stop_calls;
    return delegated_stop_result;
}
static int delegated_borrow(void *context)
{
    (void)context;
    ++delegated_borrows;
    return 1;
}
static void delegated_return(void *context)
{
    (void)context;
    ++delegated_returns;
}
static uint8_t mqtt_client_index(void *context) { (void)context; return mqtt_index; }
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
    mqtt_index = 0U;
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

static void init_app_for(ABoxNor *nor)
{
    ABoxBootV2AppPort port = {
        0, tick, submit, send_payload, begin_raw, has_pending, is_active,
        cancel, register_events, mqtt_pause, mqtt_client_index, log_line, transfer, sizeof(transfer)
    };
    ABoxBootV2AppConfig config = {
        "https://ota.example:20443", "meal_delivery_vehicle", "meal_delivery_vehicle_app.bin",
        "abox-v2-", "abox-v2-current", ca, sizeof(ca) - 1U,
        0x08008000U, 128U, 0x36800U
    };
    config.nor = nor;
    if (use_seed_gate) port.may_start_seed = may_start_seed;
    assert(ABoxBootV2App_Init(&port, &config));
}

static void init_app(void) { init_app_for(NULL); }

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
    use_seed_gate = 1; seed_allowed = 0;
    init_app();
    complete_probes();
    finish_existing_ca();
    ABoxBootV2App_Task();
    assert(!pending && !mqtt_paused && !ABoxBootV2App_IsBusy());
    seed_allowed = 1;
    use_seed_gate = 0;
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
    ABoxBootV2Record before;
    ABoxBootV2Record after;

    prepare_flash();
    reset_transport();
    init_app();
    complete_probes();
    finish_existing_ca();
    assert(ABoxBootV2_StateLoad(&before));

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
    assert(strstr(last_log, fail_on_close ? "AT+QMTCLOSE=0" : "AT+QMTDISC=0") != 0);
    assert(!mqtt_paused);
    assert(!ABoxBootV2App_IsBusy());
    assert(ABoxBootV2_StateLoad(&after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    assert(payload_count == 0U);
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

static void test_mqtt_client_quiesce(uint8_t index)
{
    ABoxBootV2AppRequest request;
    prepare_flash();
    reset_transport();
    mqtt_index = index;
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
    assert(mqtt_paused);
    assert(strcmp(command, index ? "AT+QMTDISC=1" : "AT+QMTDISC=0") == 0);
    complete(ABOX_BOOT_V2_AT_OK);
    assert(strcmp(command, index ? "AT+QMTCLOSE=1" : "AT+QMTCLOSE=0") == 0);
    complete(ABOX_BOOT_V2_AT_OK);
    assert(pending);
}

static void test_delegated_mqtt_quiesce(void)
{
    ABoxBootV2AppPort port = {
        0, tick, submit, send_payload, begin_raw, has_pending, is_active,
        cancel, register_events, mqtt_pause, mqtt_client_index, log_line,
        transfer, sizeof(transfer), 0, delegated_stop, delegated_borrow,
        delegated_return
    };
    ABoxBootV2AppConfig config = {
        "https://ota.example:20443", "meal_delivery_vehicle", "meal_delivery_vehicle_app.bin",
        "abox-v2-", "abox-v2-current", ca, sizeof(ca) - 1U,
        0x08008000U, 128U, 0x36800U
    };
    ABoxBootV2AppRequest request;
    prepare_flash();
    reset_transport();
    delegated_stop_result = 0;
    delegated_stop_calls = delegated_borrows = delegated_returns = 0U;
    assert(ABoxBootV2App_Init(&port, &config));
    complete_probes();
    finish_existing_ca();
    memset(&request, 0, sizeof(request));
    snprintf(request.url, sizeof(request.url),
             "https://ota.example:20443/fw-revisions/meal_delivery_vehicle/abox-v2-next/meal_delivery_vehicle_app.bin");
    snprintf(request.version, sizeof(request.version), "abox-v2-next");
    request.size = 128U;
    request.crc32 = 1U;
    command[0] = 0;
    assert(ABoxBootV2App_Start(&request));
    assert(mqtt_paused && !pending && command[0] == 0);
    ABoxBootV2App_Task();
    assert(delegated_stop_calls == 1U && !delegated_borrows && !pending);
    delegated_stop_result = 1;
    ABoxBootV2App_Task();
    assert(delegated_borrows == 1U && pending);
    assert(strstr(command, "QMTDISC") == 0 &&
           strstr(command, "QMTCLOSE") == 0);
    assert(delegated_returns == 0U && mqtt_paused);
}


static unsigned boot_jumps, installed_bytes, cut_at_write;
static int reset_reason;
static ABoxBootV2NorReader reader;
static uint8_t install_buffer[1024];
static jmp_buf power_cut;

static int install_word(void *context, uint32_t address, const uint8_t data[4])
{
    assert(flash_write(context, address, data, 4U));
    installed_bytes += 4U;
    if (cut_at_write && installed_bytes == cut_at_write) longjmp(power_cut, 1);
    return 1;
}

static int validate_vector(void *context, const uint8_t vector[8])
{
    (void)context;
    return ABoxBootV2_ImageVectorValid(vector);
}

static ABoxBootV2InstallResult nor_install(void *context, uint8_t slot, uint32_t size, uint32_t crc)
{
    (void)context;
    const ABoxBootV2Installer port = {
        &reader, ABoxBootV2NorReader_Open, ABoxBootV2NorReader_Read, ABoxBootV2NorReader_Close,
        flash_begin, flash_erase, install_word, flash_read, flash_end, validate_vector,
        0x08008000U, STATE_A, 0x800U, install_buffer, sizeof(install_buffer)
    };
    reader = (ABoxBootV2NorReader){.nor = &test_nor, .size = size, .crc = crc};
    return ABoxBootV2_Install(&port, slot, size, crc);
}

static uint8_t installed_valid(void *context, uint32_t size, uint32_t crc)
{
    (void)context;
    return size >= 8U && size <= STATE_A - 0x08008000U &&
           ABoxBootV2_Crc32(flash_mem + 0x8000, size) == crc;
}

static void jump_test(void *context) { (void)context; ++boot_jumps; }
static uint8_t reset_test(void *context) { (void)context; return (uint8_t)reset_reason; }

static void restart_boot(void)
{
    static const ABoxBootV2BootPort port = {
        .flash_image_valid = installed_valid, .install_slot = nor_install,
        .jump_to_app = jump_test, .reset_reason = reset_test
    };
    nor_test_open(&nor_chip, &test_nor);
    assert(ABoxBootV2Boot_Init(&port));
}

static void descriptor_for(uint32_t feature)
{
    ABoxBootV2Descriptor desc = {
        .magic = ABOX_BOOT_V2_DESCRIPTOR_MAGIC, .abi_version = ABOX_BOOT_V2_DESCRIPTOR_ABI,
        .length = sizeof(desc), .feature_flags =
            (ABOX_BOOT_V2_REQUIRED_FEATURES & ~ABOX_BOOT_V2_FEATURE_UFS_AB) | feature
    };
    desc.crc32 = ABoxBootV2_Crc32(&desc, (uint32_t)offsetof(ABoxBootV2Descriptor, crc32));
    memcpy(flash_mem + ABOX_BOOT_V2_DESCRIPTOR_ADDR - BASE, &desc, sizeof(desc));
}

static void start_nor_app(void)
{
    reset_transport();
    init_app_for(&test_nor);
    complete_probes();
    finish_existing_ca();
}

static void nor_candidate(const uint8_t *image, unsigned size, int interrupt_copy)
{
    ABoxBootV2AppRequest request = { .size = size, .crc32 = ABoxBootV2_Crc32(image, size) };
    snprintf(request.url, sizeof(request.url),
        "https://ota.example:20443/fw-revisions/meal_delivery_vehicle/abox-v2-next/meal_delivery_vehicle_app.bin");
    snprintf(request.version, sizeof(request.version), "abox-v2-next");
    assert(ABoxBootV2App_Start(&request));
    complete(ABOX_BOOT_V2_AT_OK);
    complete(ABOX_BOOT_V2_AT_OK);
    unsigned read_offset = 0U;

    for (unsigned i = 0; i < 400; ++i) {
        ABoxBootV2App_Task();
        if (ABoxBootV2App_TakeInstallReady()) {
            assert(!interrupt_copy && !mqtt_paused);
            return;
        }
        if (interrupt_copy && strcmp(ABoxBootV2App_Phase(), "NOR_COPY") == 0) {
            /* One page reached NOR, while state still selects the stable image. */
            ABoxBootV2App_Task();
            nor_test_open(&nor_chip, &test_nor);
            start_nor_app();
            return;
        }
        if (!pending) continue;
        char reply[80];
        if (strstr(command, "QHTTPURL")) {
            line("CONNECT");
            ABoxBootV2App_Task();
        } else if (!strcmp(command, "AT+QHTTPGET=80")) {
            snprintf(reply, sizeof(reply), "+QHTTPGET: 0,200,%u", size);
            line(reply);
        } else if (strstr(command, "QFLST")) {
            assert(strstr(command, "fw_nor_stage.bin"));
            snprintf(reply, sizeof(reply), "+QFLST: \"UFS:fw_nor_stage.bin\",%u", size);
            line(reply);
        } else if (strstr(command, "QFOPEN")) {
            assert(strstr(command, "fw_nor_stage.bin"));
            read_offset = 0U;
            line("+QFOPEN: 8");
        } else if (strstr(command, "QFREAD=8,")) {
            unsigned length;
            assert(sscanf(command, "AT+QFREAD=8,%u", &length) == 1);
            assert(length && length <= size - read_offset);
            snprintf(reply, sizeof(reply), "CONNECT %u", length);
            line(reply);
            assert(raw_wanted == length);
            raw(image + read_offset, (uint16_t)(length / 2U));
            raw(image + read_offset + length / 2U, (uint16_t)(length - length / 2U));
            read_offset += length;
        }
        if (!strcmp(command, "AT+QHTTPSTOP") || !strcmp(command, "AT+QIDEACT=1") ||
            strstr(command, "QFDEL"))
            complete(ABOX_BOOT_V2_AT_ERROR);
        else
            complete(ABOX_BOOT_V2_AT_OK);
    }
    assert(0);
}

static void test_nor_ota(void)
{
    static const uint8_t vectors[8] = {0, 0x40, 0, 0x20, 1, 0x81, 0, 0x08};
    static ABoxMqttIdemNor idem;
    const uint8_t digest[32] = {9U};
    ABoxClockSnapshot clock = {.quality = ABOX_CLOCK_SYNCHRONIZED, .utc_ms = 1800000000000ULL};
    uint8_t stable[128], candidate[1457];
    char saved_responses[4][1024];
    const unsigned response_count = ABOX_IDEM_NOR_LOG_SECTORS > 1U ? 4U : 3U;
    ABoxBootV2Record state;
    size_t handle;
    nor_test_fresh(&nor_chip, &test_nor);
    memset(flash_mem, 0xFF, sizeof(flash_mem));
    memset(stable, 0x42, sizeof(stable));
    memset(candidate, 0x63, sizeof(candidate));
    memcpy(stable, vectors, sizeof(vectors));
    memcpy(candidate, vectors, sizeof(vectors));
    memcpy(flash_mem + 0x8000, stable, sizeof(stable));

    assert(ABoxMqttIdemNor_Init(&idem, &test_nor, "device-1"));
    for (unsigned i = 0; i < 1000 && !idem.cache.ready; ++i) ABoxMqttIdem_Poll(&idem.cache);
    for (unsigned i = 0U; i < response_count; ++i) {
        char id[20];
        snprintf(id, sizeof(id), "ota-survivor-%u", i);
        memset(saved_responses[i], (int)('a' + i), sizeof(saved_responses[i]));
        assert(ABoxMqttIdem_Begin(&idem.cache, id, digest, clock.utc_ms + 5000,
                                &clock, &handle) == 200);
        ABoxMqttIdem_Poll(&idem.cache);
        ABoxMqttIdem_Poll(&idem.cache);
        assert(ABoxMqttIdem_TakeExecutable(&idem.cache, handle, &clock) == 200);
        assert(ABoxMqttIdem_Complete(&idem.cache, handle, saved_responses[i], sizeof(saved_responses[i])));
        for (unsigned pass = 0U; pass < 8U; ++pass) ABoxMqttIdem_Poll(&idem.cache);
        assert(ABoxMqttIdem_State(&idem.cache, handle) == ABOX_IDEM_FINAL);
    }

    descriptor_for(ABOX_BOOT_V2_FEATURE_UFS_AB);
    start_nor_app();
    ABoxBootV2App_Task();
    assert(!ABoxBootV2App_IsReady() && !pending);
    assert(!ABoxBootV2_StateLoad(&state));

    descriptor_for(ABOX_BOOT_V2_FEATURE_NOR_AB);
    ABoxBootV2Descriptor desc;
    assert(ABoxBootV2_DescriptorRead(&desc) && !ABoxBootV2_DescriptorReady(&desc));
    ABoxBootV2App_Task();
    complete(ABOX_BOOT_V2_AT_OK);
    complete(ABOX_BOOT_V2_AT_OK);
    for (unsigned i = 0; i < 30 && ABoxBootV2App_IsBusy(); ++i) ABoxBootV2App_Task();
    assert(ABoxBootV2App_IsReady() && !mqtt_paused);
    assert(ABoxBootV2_StateLoad(&state) && state.state == ABOX_BOOT_V2_CONFIRMED);
    assert(state.stable_slot == 0 && state.stable_image_crc32 == ABoxBootV2_Crc32(stable, sizeof(stable)));

    nor_candidate(candidate, sizeof(candidate), 1);
    assert(ABoxBootV2_StateLoad(&state) && state.state == ABOX_BOOT_V2_CONFIRMED);
    assert(!ABoxBootV2Nor_Matches(&test_nor, 1, sizeof(candidate),
                                ABoxBootV2_Crc32(candidate, sizeof(candidate))));
    nor_candidate(candidate, sizeof(candidate), 0);
    assert(ABoxBootV2_StateLoad(&state) && state.state == ABOX_BOOT_V2_INSTALL_PENDING);
    assert(state.stable_slot == 0 && state.candidate_slot == 1);

    restart_boot();
    installed_bytes = boot_jumps = 0;
    cut_at_write = 32U;
    if (setjmp(power_cut) == 0) {
        ABoxBootV2Boot_Task();
        assert(0);
    }
    assert(ABoxBootV2_StateLoad(&state) && state.state == ABOX_BOOT_V2_INSTALLING && !boot_jumps);
    cut_at_write = 0;
    restart_boot();
    ABoxBootV2Boot_Task();
    assert(ABoxBootV2_StateLoad(&state) && state.state == ABOX_BOOT_V2_TRIAL && boot_jumps == 1);
    assert(!memcmp(flash_mem + 0x8000U, candidate, sizeof(candidate)));

    reset_reason = 3;
    for (unsigned i = 0; i < 4; ++i) {
        restart_boot();
        ABoxBootV2Boot_Task();
    }
    assert(ABoxBootV2_StateLoad(&state) && state.state == ABOX_BOOT_V2_ROLLBACK && state.rollback_installed);
    assert(!memcmp(flash_mem + 0x8000U, stable, sizeof(stable)));
    assert(ABoxBootV2_AcknowledgeRollback());

    /* Re-mount following the rollback and replay byte for byte. */
    assert(ABoxMqttIdemNor_Init(&idem, &test_nor, "device-1"));
    for (unsigned i = 0; i < 1000 && !idem.cache.ready; ++i) ABoxMqttIdem_Poll(&idem.cache);
    for (unsigned i = 0U; i < response_count; ++i) {
        char id[20];
        size_t length;
        snprintf(id, sizeof(id), "ota-survivor-%u", i);
        assert(ABoxMqttIdem_Find(&idem.cache, id, digest, &handle) == ABOX_IDEM_REPLAY);
        const char *saved = ABoxMqttIdem_Response(&idem.cache, handle, &length);
        assert(saved && length == sizeof(saved_responses[i]) && !memcmp(saved, saved_responses[i], length));
        assert(ABoxMqttIdem_TakeExecutable(&idem.cache, handle, &clock) == 4003);
    }

    reset_reason = 0;
    start_nor_app();
    nor_candidate(candidate, sizeof(candidate), 0);
    restart_boot();
    ABoxBootV2Boot_Task();
    assert(ABoxBootV2_Confirm());
    assert(ABoxBootV2_StateLoad(&state) && state.state == ABOX_BOOT_V2_CONFIRMED);
    assert(state.stable_slot == 1 && !memcmp(flash_mem + 0x8000U, candidate, sizeof(candidate)));
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
    test_candidate_download(ABOX_BOOT_V2_AT_ERROR, ABOX_BOOT_V2_AT_OK);
    test_candidate_download(ABOX_BOOT_V2_AT_OK, ABOX_BOOT_V2_AT_ERROR);
    test_candidate_download(ABOX_BOOT_V2_AT_ERROR, ABOX_BOOT_V2_AT_ERROR);
    test_mqtt_client_quiesce(0U);
    test_mqtt_client_quiesce(1U);
    test_delegated_mqtt_quiesce();
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_TIMEOUT, 0U);
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_TIMEOUT, 1U);
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_SEND_FAIL, 0U);
    test_download_failure_waits_before_retry(ABOX_BOOT_V2_AT_SEND_FAIL, 1U);
    test_nor_ota();
    return 0;
}
