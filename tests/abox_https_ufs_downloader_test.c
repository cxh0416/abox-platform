#include "abox_https_ufs_downloader.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(1); \
} } while (0)

typedef struct {
    ABoxHttpsUfsDoneFn done;
    void *done_user;
    char command[320];
    char last_log[256];
    uint32_t tick;
    uint8_t active;
    uint8_t direct_failures;
    uint8_t direct_get_failures;
    uint8_t corrupt_first_verify;
    uint8_t always_corrupt;
    uint8_t range_started;
    uint8_t delete_errors;
    uint8_t invalid_vector;
    uint8_t write_failures;
    uint8_t image[32];
    uint32_t image_size;
    uint32_t direct_response_size;
    uint32_t listed_size;
    uint32_t overflow_count;
    uint32_t pdp_recovery_count;
    uint8_t cipher_configured;
} Mock;

static uint32_t tick_ms(void *context) { return ((Mock *)context)->tick; }
static int submit(void *context, const char *command, uint32_t timeout_ms,
                  ABoxHttpsUfsDoneFn done, void *user)
{
    Mock *m = (Mock *)context;
    (void)timeout_ms;
    assert(m->done == 0);
    (void)snprintf(m->command, sizeof(m->command), "%s", command);
    m->done = done;
    m->done_user = user;
    m->active = 1U;
    return 1;
}
static int send_payload(void *context, const uint8_t *data, uint16_t length)
{
    (void)data;
    (void)length;
    ((Mock *)context)->active = 1U;
    return 1;
}
static int begin_raw(void *context, uint32_t length)
{
    (void)context;
    return length > 0U && length <= ABOX_HTTPS_UFS_RAW_CHUNK;
}
static int has_pending(void *context) { return ((Mock *)context)->done != 0; }
static int is_active(void *context) { return ((Mock *)context)->active; }
static void cancel(void *context)
{
    Mock *m = (Mock *)context;
    m->done = 0;
    m->done_user = 0;
    m->active = 0U;
}
static void log_line(void *context, uint8_t level, const char *message)
{
    (void)level;
    (void)snprintf(((Mock *)context)->last_log, sizeof(((Mock *)context)->last_log), "%s", message);
    puts(message);
}
static uint32_t rx_overflow(void *context)
{
    return ((Mock *)context)->overflow_count;
}
static int vector_valid(void *context, const uint8_t vector[8])
{
    Mock *m = (Mock *)context;
    return !m->invalid_vector && vector[0] == 0x00U && vector[1] == 0x10U;
}
static uint32_t crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    while (length--) {
        uint8_t bit;
        crc ^= *data++;
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}
static void line(ABoxHttpsUfsDownloader *d, const char *value)
{
    ABoxHttpsUfs_OnEvent(d, ABOX_HTTPS_UFS_AT_LINE,
                         (const uint8_t *)value, (uint16_t)strlen(value));
}
static void finish(Mock *m, ABoxHttpsUfsAtResult result)
{
    ABoxHttpsUfsDoneFn done = m->done;
    void *user = m->done_user;
    assert(done != 0);
    m->done = 0;
    m->done_user = 0;
    m->active = 0U;
    m->tick += 5U;
    done(result, user);
}

static void service_command(ABoxHttpsUfsDownloader *d, Mock *m)
{
    char current[sizeof(m->command)];
    (void)snprintf(current, sizeof(current), "%s", m->command);
    printf("CMD %s\n", current);
    if (strcmp(current, "AT+QIDEACT=1") == 0)
        ++m->pdp_recovery_count;
    if (strcmp(current, "AT+QSSLCFG=\"ciphersuite\",1,0xFFFF") == 0)
        m->cipher_configured = 1U;
    if (strncmp(current, "AT+QHTTPGET", 11U) == 0)
        assert(m->cipher_configured); /* An enrollment session left RSA only. */
    if (strncmp(current, "AT+QHTTPURL=", 12U) == 0 ||
        strncmp(current, "AT+QFWRITE=", 11U) == 0)
        ABoxHttpsUfs_Task(d);
    if (strcmp(current, "AT+QHTTPGET=80") == 0) {
        char response[48];
        (void)snprintf(response, sizeof(response), "+QHTTPGET: 0,200,%lu",
                       (unsigned long)m->direct_response_size);
        line(d, response);
    }
    else if (strncmp(current, "AT+QHTTPGETEX=", 14U) == 0)
    {
        m->range_started = 1U;
        line(d, "+QHTTPGET: 0,206,32");
    }
    else if (strncmp(current, "AT+QFOPEN=", 10U) == 0)
        line(d, "+QFOPEN: 1");
    else if (strncmp(current, "AT+QFLST=", 9U) == 0) {
        char response[64];
        (void)snprintf(response, sizeof(response),
                       "+QFLST: \"fw_slot1.bin\",%lu",
                       (unsigned long)m->listed_size);
        line(d, response);
    }
    else if (strcmp(current, "AT+QHTTPREAD=80") == 0) {
        line(d, "CONNECT");
        ABoxHttpsUfs_OnEvent(d, ABOX_HTTPS_UFS_AT_RAW, m->image,
                             (uint16_t)m->image_size);
    } else if (strncmp(current, "AT+QFWRITE=", 11U) == 0)
        line(d, "+QFWRITE: 32,32");
    else if (strncmp(current, "AT+QFREAD=", 10U) == 0) {
        uint8_t verify[sizeof(m->image)];
        memcpy(verify, m->image, sizeof(verify));
        if (m->always_corrupt ||
            (m->corrupt_first_verify && !m->range_started))
            verify[10] ^= 0x5AU;
        line(d, "CONNECT 32");
        ABoxHttpsUfs_OnEvent(d, ABOX_HTTPS_UFS_AT_RAW, verify,
                             (uint16_t)m->image_size);
    }

    if (strcmp(current, "AT+QHTTPGET=80") == 0 &&
        m->direct_get_failures) {
        --m->direct_get_failures;
        finish(m, ABOX_HTTPS_UFS_AT_ERROR);
    } else if (strncmp(current, "AT+QFDEL=", 10U) == 0 && m->delete_errors) {
        --m->delete_errors;
        finish(m, ABOX_HTTPS_UFS_AT_ERROR);
    } else if (strncmp(current, "AT+QFWRITE=", 11U) == 0 && m->write_failures) {
        --m->write_failures;
        finish(m, ABOX_HTTPS_UFS_AT_ERROR);
    } else if (strncmp(current, "AT+QHTTPREADFILE=", 17U) == 0 &&
        m->direct_failures) {
        --m->direct_failures;
        finish(m, ABOX_HTTPS_UFS_AT_ERROR);
    } else
        finish(m, ABOX_HTTPS_UFS_AT_OK);
}

static void run_until_done(ABoxHttpsUfsDownloader *d, Mock *m)
{
    uint32_t guard = 1000U;
    while (ABoxHttpsUfs_IsBusy(d) && guard--) {
        if (m->done) service_command(d, m);
        else ABoxHttpsUfs_Task(d);
    }
    assert(guard != 0U);
}

static void setup(ABoxHttpsUfsDownloader *d, Mock *m,
                  uint8_t direct_supported)
{
    static uint8_t transfer[ABOX_HTTPS_UFS_RAW_CHUNK];
    ABoxHttpsUfsPort port;
    ABoxHttpsUfsConfig config;
    ABoxHttpsUfsRequest request;
    memset(m, 0, sizeof(*m));
    m->image_size = 32U;
    m->direct_response_size = 32U;
    m->listed_size = 32U;
    m->image[0] = 0x00U;
    m->image[1] = 0x10U;
    m->image[4] = 0x01U;
    memset(&port, 0, sizeof(port));
    port.context = m;
    port.tick_ms = tick_ms;
    port.submit = submit;
    port.send_payload = send_payload;
    port.begin_raw = begin_raw;
    port.has_pending = has_pending;
    port.is_active = is_active;
    port.cancel = cancel;
    port.log = log_line;
    port.rx_overflow_count = rx_overflow;
    memset(&config, 0, sizeof(config));
    config.ca_file = "UFS:ota_ca.pem";
    config.transfer_buffer = transfer;
    config.transfer_buffer_size = sizeof(transfer);
    config.vector_valid = vector_valid;
    config.vector_context = m;
    config.direct_supported = direct_supported;
    assert(ABoxHttpsUfs_Init(d, &port, &config));
    memset(&request, 0, sizeof(request));
    request.url = "https://example/fw-revisions/p/v/app.bin";
    request.file = "fw_slot1.bin";
    request.size = m->image_size;
    request.crc32 = crc32(m->image, m->image_size);
    assert(ABoxHttpsUfs_Start(d, &request));
}

static void test_direct_success(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    setup(&d, &m, 1U);
    m.overflow_count = 2U;
    run_until_done(&d, &m);
    assert(ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_Metrics(&d)->backend == ABOX_HTTPS_UFS_BACKEND_DIRECT);
    assert(ABoxHttpsUfs_Metrics(&d)->fallback_reason == ABOX_HTTPS_UFS_FALLBACK_NONE);
    assert(ABoxHttpsUfs_Metrics(&d)->rx_overflow == 2U);
    assert(m.pdp_recovery_count == 0U);
}

static void test_integrity_failure_goes_directly_to_range(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    setup(&d, &m, 1U);
    m.corrupt_first_verify = 1U;
    run_until_done(&d, &m);
    assert(ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_Metrics(&d)->backend == ABOX_HTTPS_UFS_BACKEND_RANGE);
    assert(ABoxHttpsUfs_Metrics(&d)->fallback_reason == ABOX_HTTPS_UFS_FALLBACK_INTEGRITY);
    assert(ABoxHttpsUfs_Metrics(&d)->direct_attempts == 1U);
}

static void test_second_integrity_failure_is_final(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    uint32_t error = 0U;
    setup(&d, &m, 1U);
    m.always_corrupt = 1U;
    run_until_done(&d, &m);
    assert(!ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_TakeFailure(&d, &error));
    assert(error == ABOX_HTTPS_UFS_ERROR_CRC);
}

static void test_direct_failure_falls_back(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    setup(&d, &m, 1U);
    m.direct_failures = 2U;
    run_until_done(&d, &m);
    assert(ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_Metrics(&d)->backend == ABOX_HTTPS_UFS_BACKEND_RANGE);
    assert(ABoxHttpsUfs_Metrics(&d)->fallback_reason == ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR);
    /* One PDP recovery precedes the direct retry and another sanitizes the
     * truncated whole-response session before Range starts. */
    assert(m.pdp_recovery_count == 2U);
    assert(ABoxHttpsUfs_Metrics(&d)->ufs_store_ms > 0U);
}

static void test_truncated_direct_get_recovers_before_range(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    setup(&d, &m, 1U);
    m.direct_get_failures = 2U;
    run_until_done(&d, &m);
    assert(ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_Metrics(&d)->backend == ABOX_HTTPS_UFS_BACKEND_RANGE);
    assert(ABoxHttpsUfs_Metrics(&d)->fallback_reason == ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR);
    assert(ABoxHttpsUfs_Metrics(&d)->direct_attempts == 2U);
    assert(m.pdp_recovery_count == 2U);
}

static void test_existing_candidate_delete_error_is_tolerated(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    setup(&d, &m, 1U);
    m.delete_errors = 1U;
    run_until_done(&d, &m);
    assert(ABoxHttpsUfs_TakeSuccess(&d));
}

static void test_content_length_mismatch_falls_back(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    setup(&d, &m, 1U);
    m.direct_response_size = 31U;
    run_until_done(&d, &m);
    assert(ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_Metrics(&d)->backend == ABOX_HTTPS_UFS_BACKEND_RANGE);
    assert(ABoxHttpsUfs_Metrics(&d)->fallback_reason == ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR);
}

static void test_vector_failure_never_succeeds(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    uint32_t error = 0U;
    setup(&d, &m, 1U);
    m.invalid_vector = 1U;
    run_until_done(&d, &m);
    assert(!ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_TakeFailure(&d, &error));
    assert(error == ABOX_HTTPS_UFS_ERROR_VECTOR);
}

static void test_file_size_failure_never_succeeds(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    uint32_t error = 0U;
    setup(&d, &m, 1U);
    m.listed_size = 31U;
    run_until_done(&d, &m);
    assert(!ABoxHttpsUfs_TakeSuccess(&d));
    assert(ABoxHttpsUfs_TakeFailure(&d, &error));
    assert(error == ABOX_HTTPS_UFS_ERROR_SIZE);
}

static void test_ufs_error_log_identifies_command(void)
{
    ABoxHttpsUfsDownloader d;
    Mock m;
    uint32_t error = 0U;
    setup(&d, &m, 0U);
    m.write_failures = 1U;
    run_until_done(&d, &m);
    assert(ABoxHttpsUfs_TakeFailure(&d, &error));
    assert(error == ABOX_HTTPS_UFS_ERROR_UFS);
    assert(strstr(m.last_log, "phase=DOWNLOADING_RANGE") != 0);
    assert(strstr(m.last_log, "command=AT+QFWRITE=") != 0);
}

int main(void)
{
    for (unsigned backend=0; backend<2; ++backend) {
        ABoxHttpsUfsDownloader d; Mock m; uint32_t error;
        setup(&d, &m, (uint8_t)backend);
        d.handle_valid=1U;
        ABoxHttpsUfsDoneFn stale=m.done; void *user=m.done_user;
        ABoxHttpsUfs_OnModemReset(&d);
        assert(!ABoxHttpsUfs_IsBusy(&d) && !d.handle_valid && !m.done);
        assert(ABoxHttpsUfs_TakeFailure(&d, &error) && error==ABOX_HTTPS_UFS_ERROR_UFS);
        ABoxHttpsUfs_OnModemReset(&d);
        if (stale) stale(ABOX_HTTPS_UFS_AT_OK, user);
        assert(!ABoxHttpsUfs_TakeFailure(&d, &error));
        assert(!ABoxHttpsUfs_TakeSuccess(&d));
    }
    test_direct_success();
    test_direct_failure_falls_back();
    test_truncated_direct_get_recovers_before_range();
    test_existing_candidate_delete_error_is_tolerated();
    test_content_length_mismatch_falls_back();
    test_integrity_failure_goes_directly_to_range();
    test_second_integrity_failure_is_final();
    test_vector_failure_never_succeeds();
    test_file_size_failure_never_succeeds();
    test_ufs_error_log_identifies_command();
    return 0;
}
