#include "abox_ec800_at.h"
#include "abox_ec800_rx.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(1); \
} } while (0)

static uint32_t g_tick;
static char g_tx[512];
static uint16_t g_tx_length;
static uint32_t g_done_count;
static ABoxEc800Result g_done_result;
static uint32_t g_line_count;
static uint16_t g_last_line_length;
static uint32_t g_raw_count;
static uint32_t g_sink_count;

static uint32_t tick_ms(void *context) { (void)context; return g_tick; }
static int write_data(void *context, const uint8_t *data, uint16_t length,
                      uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    assert(length <= sizeof(g_tx));
    memcpy(g_tx, data, length);
    g_tx_length = length;
    return 1;
}
static void done(ABoxEc800Result result, void *user)
{
    (void)user;
    ++g_done_count;
    g_done_result = result;
}
static void event(ABoxEc800Event type, const uint8_t *data, uint16_t length,
                  void *user)
{
    (void)data;
    (void)user;
    if (type == ABOX_EC800_EVENT_RAW) g_raw_count += length;
    else {
        ++g_line_count;
        g_last_line_length = length;
    }
}
static void sink(void *context, const uint8_t *data, uint16_t length)
{
    ABoxEc800At_Feed((ABoxEc800At *)context, data, length);
}
static void count_sink(void *context, const uint8_t *data, uint16_t length)
{
    (void)context;
    (void)data;
    g_sink_count += length;
}
static void counted_event(ABoxEc800Event type, const uint8_t *data,
                          uint16_t length, void *user)
{
    uint32_t *count = (uint32_t *)user;
    (void)data;
    (void)length;
    if (type == ABOX_EC800_EVENT_LINE) ++*count;
}

static void test_async_direct_command(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    const char response[] = "OK\r\n+QHTTPGET: 0,200,16\r\n";
    memset(&at, 0, sizeof(at));
    g_done_count = 0U;
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_OTA, event, 0));
    assert(ABoxEc800At_Submit(&at, "AT+QHTTPGET=80", ABOX_EC800_OWNER_OTA,
                              ABOX_EC800_PRIORITY_HIGH, 1000U, done, 0));
    ABoxEc800At_Task(&at);
    assert(g_tx_length && strstr(g_tx, "AT+QHTTPGET=80") != 0);
    ABoxEc800At_Feed(&at, (const uint8_t *)response,
                     (uint16_t)strlen(response));
    assert(g_done_count == 1U && g_done_result == ABOX_EC800_RESULT_OK);
}

static void test_http_post_waits_for_payload_and_result_urc(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    const uint8_t payload[] = "{\"uid\":\"0011\"}";
    memset(&at, 0, sizeof(at));
    memset(g_tx, 0, sizeof(g_tx));
    g_done_count = 0U;
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Submit(&at, "AT+QHTTPPOST=14,80,80",
                              ABOX_EC800_OWNER_PRODUCT_BASE,
                              ABOX_EC800_PRIORITY_HIGH, 1000U, done, 0));
    ABoxEc800At_Task(&at);
    assert(strstr(g_tx, "AT+QHTTPPOST=14,80,80") != 0);
    ABoxEc800At_Feed(&at, (const uint8_t *)"CONNECT\r\n", 9U);
    assert(ABoxEc800At_SendPayload(&at, ABOX_EC800_OWNER_PRODUCT_BASE,
                                   payload, sizeof(payload) - 1U));
    ABoxEc800At_Feed(&at, (const uint8_t *)"OK\r\n", 4U);
    assert(g_done_count == 0U);
    {
        const char result[] = "+QHTTPPOST: 0,201,512\r\n";
        ABoxEc800At_Feed(&at, (const uint8_t *)result,
                         (uint16_t)(sizeof(result) - 1U));
    }
    assert(g_done_count == 1U && g_done_result == ABOX_EC800_RESULT_OK);
}

static void test_raw_split_and_dynamic_owner(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    uint8_t raw[1024];
    memset(raw, 0xA5, sizeof(raw));
    g_raw_count = 0U;
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_PRODUCT_BASE, event, 0));
    assert(ABoxEc800At_BeginRaw(&at, ABOX_EC800_OWNER_PRODUCT_BASE, sizeof(raw)));
    ABoxEc800At_Feed(&at, raw, 37U);
    ABoxEc800At_Feed(&at, raw + 37U, (uint16_t)(sizeof(raw) - 37U));
    assert(g_raw_count == sizeof(raw));

    g_raw_count = 0U;
    assert(ABoxEc800At_BeginRaw(&at, ABOX_EC800_OWNER_PRODUCT_BASE, 981U));
    ABoxEc800At_Feed(&at, raw, 333U);
    ABoxEc800At_Feed(&at, raw + 333U, 648U);
    assert(g_raw_count == 981U);
}

static void test_owner_routing_and_raw_urc_glue(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0, 0};
    uint32_t mqtt_lines = 0U;
    uint32_t ota_lines = 0U;
    uint8_t glued[1024U + 8U];
    memset(glued, 0xA5, 1024U);
    memcpy(glued + 1024U, "OK\r\n", 4U);
    g_done_count = 0U;
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_MQTT,
                                counted_event, &mqtt_lines));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_OTA,
                                counted_event, &ota_lines));
    assert(ABoxEc800At_Submit(&at, "AT+QHTTPGET=80", ABOX_EC800_OWNER_OTA,
                              ABOX_EC800_PRIORITY_HIGH, 1000U, done, 0));
    ABoxEc800At_Task(&at);
    ABoxEc800At_Feed(&at, (const uint8_t *)"+QMTSTAT: 1,3\r\n", 16U);
    assert(mqtt_lines == 1U && ota_lines == 0U);
    g_raw_count = 0U;
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_PRODUCT_BASE, event, 0));
    assert(ABoxEc800At_BeginRaw(&at, ABOX_EC800_OWNER_PRODUCT_BASE, 1024U));
    ABoxEc800At_Feed(&at, glued, 1028U);
    assert(g_raw_count == 1024U);
    ABoxEc800At_AbortAll(&at, ABOX_EC800_RESULT_ERROR);
    assert(g_done_count == 1U && g_done_result == ABOX_EC800_RESULT_ERROR);
}

static void test_dma_half_full_and_wrap(void)
{
    ABoxEc800Rx rx;
    uint8_t dma[ABOX_EC800_RX_DMA_MIN];
    uint8_t ring[ABOX_EC800_RX_RING_MIN];
    uint16_t half = (uint16_t)(sizeof(dma) / 2U);
    memset(dma, 0x11, half);
    memset(dma + half, 0x22, sizeof(dma) - half);
    g_sink_count = 0U;
    assert(ABoxEc800Rx_Init(&rx, dma, sizeof(dma), ring, sizeof(ring)));
    assert(ABoxEc800Rx_OnDmaEvent(&rx, half));
    assert(ABoxEc800Rx_Drain(&rx, count_sink, 0, 0U) == half);
    assert(ABoxEc800Rx_OnDmaEvent(&rx, (uint16_t)sizeof(dma)));
    assert(ABoxEc800Rx_Drain(&rx, count_sink, 0, 0U) == sizeof(dma) - half);
    memset(dma, 0x33, 200U);
    assert(ABoxEc800Rx_OnDmaEvent(&rx, 200U));
    assert(ABoxEc800Rx_Drain(&rx, count_sink, 0, 0U) == 200U);
    assert(g_sink_count == sizeof(dma) + 200U);
    assert(ABoxEc800Rx_MaxBacklog(&rx) >= half);
}

static void test_circular_dma_wrap_and_overflow(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    ABoxEc800Rx rx;
    uint8_t dma[ABOX_EC800_RX_DMA_MIN];
    uint8_t ring[ABOX_EC800_RX_RING_MIN];
    const char line[] = "+QMTRECV: 0,0\r\n";
    memset(dma, 0, sizeof(dma));
    memcpy(dma, line, sizeof(line) - 1U);
    g_line_count = 0U;
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_MQTT, event, 0));
    assert(ABoxEc800Rx_Init(&rx, dma, sizeof(dma), ring, sizeof(ring)));
    assert(ABoxEc800Rx_OnDmaEvent(&rx, (uint16_t)(sizeof(line) - 1U)));
    assert(ABoxEc800Rx_Drain(&rx, sink, &at, 0U) == sizeof(line) - 1U);
    assert(g_line_count == 1U);

    rx.head = rx.ring_size;
    rx.tail = 0U;
    rx.dma_position = 0U;
    assert(!ABoxEc800Rx_OnDmaEvent(&rx, 1U));
    assert(ABoxEc800Rx_OverflowCount(&rx) == 1U);
    assert(ABoxEc800Rx_TakeRestartRequest(&rx));
    ABoxEc800Rx_AfterRestart(&rx);
}

static void test_overlong_line_is_dropped_and_next_urc_recovers(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    uint8_t input[ABOX_EC800_AT_LINE_SIZE + 64U];
    const char next[] = "+QMTSTAT: 0,3\r\n";
    size_t oversized = ABOX_EC800_AT_LINE_SIZE + 8U;

    memset(input, 'A', oversized);
    input[oversized++] = '\r';
    input[oversized++] = '\n';
    memcpy(input + oversized, next, sizeof(next) - 1U);
    oversized += sizeof(next) - 1U;

    g_line_count = 0U;
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_MQTT, event, 0));
    ABoxEc800At_Feed(&at, input, (uint16_t)oversized);
    assert(ABoxEc800At_LineOverflowCount(&at) == 1U);
    assert(g_line_count == 1U);
}

static void test_internal_mqtt_max_payload_line_is_delivered(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    uint8_t input[ABOX_EC800_AT_LINE_SIZE];
    const char prefix[] = "+QMTRECV: 0,0,\"/zxwl/abox/device/internal/ota\",";
    size_t length = sizeof(prefix) - 1U;

    memcpy(input, prefix, length);
    memset(input + length, 'A', 1280U);
    length += 1280U;
    input[length++] = '\r';
    input[length++] = '\n';

    g_line_count = 0U;
    g_last_line_length = 0U;
    assert(length <= sizeof(input));
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_MQTT, event, 0));
    ABoxEc800At_Feed(&at, input, (uint16_t)length);
    assert(g_line_count == 1U);
    assert(g_last_line_length == (uint16_t)(length - 2U));
}

static void test_modem_ready_reaches_all_owners_during_active_command(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    uint32_t mqtt_lines = 0U, tls_lines = 0U;
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_MQTT,
                                counted_event, &mqtt_lines));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_PRODUCT_BASE + 3U,
                                counted_event, &tls_lines));
    assert(ABoxEc800At_Submit(&at, "AT+QHTTPGET=80", ABOX_EC800_OWNER_OTA,
                              ABOX_EC800_PRIORITY_HIGH, 1000U, done, 0));
    ABoxEc800At_Task(&at);
    assert(at.active_valid);
    ABoxEc800At_Feed(&at, (const uint8_t *)"RDY\r\n", 5U);
    assert(mqtt_lines == 1U && tls_lines == 1U);
    assert(!at.active_valid && !at.quarantined);
    assert(ABoxEc800At_Submit(&at, "AT", ABOX_EC800_OWNER_MQTT,
                              ABOX_EC800_PRIORITY_NORMAL, 1000U, done, 0));
}

static void test_credentials_never_reach_line_observers(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    uint32_t observed = 0U;
    const char *lines = "AT+QSSLCFG=\"psk\",0,\"device\",\"public-test-fixture-only\"\r\n"
        "+QSSLCFG: \"psk\",0,\"device\",\"public-test-fixture-only\"\r\n"
        "AT+QMTCONN=0,\"device\",\"user\",\"test-password\"\r\n";
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_MQTT, counted_event, &observed));
    ABoxEc800At_Feed(&at, (const uint8_t *)lines, (uint16_t)strlen(lines));
    assert(observed == 0U);
    ABoxEc800At_Feed(&at, (const uint8_t *)"+QMTCONN: 0,0,0\r\n", 17U);
    assert(observed == 1U);
}

typedef struct { ABoxEc800At *at; unsigned calls; } Termination;
static void terminated(ABoxEc800Result result, void *user)
{
    Termination *t = user;
    assert(result == ABOX_EC800_RESULT_ERROR);
    assert(!ABoxEc800At_IsBusy(t->at));
    ++t->calls;
    assert(!ABoxEc800At_Submit(t->at, "AT", 29U,
        ABOX_EC800_PRIORITY_NORMAL, 100U, terminated, t));
    assert(!ABoxEc800At_Register(t->at, 29U, event, NULL));
    ABoxEc800At_Task(t->at);
    ABoxEc800At_Feed(t->at, (const uint8_t *)"RDY\r\nOK\r\n", 9U);
    ABoxEc800At_AbortAll(t->at, result);
    ABoxEc800At_Reset(t->at);
}
static void test_bulk_termination(void)
{
    unsigned mode, active, i;
    for (mode = 0; mode < 3; ++mode) for (active = 0; active < 2; ++active) {
        ABoxEc800At at;
        ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
        Termination ops[ABOX_EC800_AT_QUEUE_SIZE + 1U];
        assert(ABoxEc800At_Init(&at, &port));
        for (i = 0; i < ABOX_EC800_AT_QUEUE_SIZE + active; ++i) {
            ops[i] = (Termination){&at, 0};
            assert(ABoxEc800At_Submit(&at, "AT", (ABoxEc800Owner)(20U+i),
                ABOX_EC800_PRIORITY_NORMAL, 1000U, terminated, &ops[i]));
            if (!i && active) ABoxEc800At_Task(&at);
        }
        for (i = 0; i < 2; ++i) {
            if (!mode) ABoxEc800At_Feed(&at, (const uint8_t *)"RDY\r\nOK\r\n", 9U);
            else if (mode == 1) ABoxEc800At_AbortAll(&at, ABOX_EC800_RESULT_ERROR);
            else ABoxEc800At_Quarantine(&at); /* RX parser/overflow isolation */
        }
        for (i = 0; i < ABOX_EC800_AT_QUEUE_SIZE + active; ++i) assert(ops[i].calls == 1U);
        assert(!ABoxEc800At_IsBusy(&at));
        if (mode) assert(at.quarantined);
        ABoxEc800At_Reset(&at);
        assert(!at.quarantined);
        g_done_count = 0U;
        assert(ABoxEc800At_Submit(&at, "AT", 20U, ABOX_EC800_PRIORITY_NORMAL, 1000U, done, 0));
        ABoxEc800At_Task(&at);
        ABoxEc800At_Feed(&at, (const uint8_t *)"OK\r\n", 4U);
        assert(g_done_count == 1U && g_done_result == ABOX_EC800_RESULT_OK);
    }
}
static void reset_during_raw(ABoxEc800Event e, const uint8_t *d, uint16_t n, void *user)
{
    (void)d; (void)n;
    if (e == ABOX_EC800_EVENT_RAW) ABoxEc800At_Reset(user);
}
static void test_raw_reset_does_not_underflow(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0};
    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, 20U, reset_during_raw, &at));
    assert(ABoxEc800At_BeginRaw(&at, 20U, 10U));
    ABoxEc800At_Feed(&at, (const uint8_t *)"12345", 5U);
    assert(!at.raw_remaining && !at.raw_owner);
}

static int fail_write;
static int injected_write(void *u, const uint8_t *d, uint16_t n, uint32_t t)
{ return fail_write ? 0 : write_data(u,d,n,t); }
static void tx_failed(ABoxEc800Result result, void *user)
{
    assert(result==ABOX_EC800_RESULT_SEND_FAIL);
    ++*(unsigned *)user;
}
static void failed_payload(ABoxEc800Event e, const uint8_t *d, uint16_t n, void *u)
{
    (void)d;
    if (e==ABOX_EC800_EVENT_LINE && n==1) {
        fail_write=1;
        assert(!ABoxEc800At_SendPayload(u,20,(const uint8_t *)"payload",7));
    }
}
static void test_failed_write_terminates_session(void)
{
    for (unsigned payload=0;payload<2;++payload) {
        ABoxEc800At at;
        ABoxEc800AtPort p={.tick_ms=tick_ms,.write=injected_write};
        unsigned completions[2]={0};
        fail_write=0;
        assert(ABoxEc800At_Init(&at,&p));
        assert(ABoxEc800At_Register(&at,20,failed_payload,&at));
        assert(ABoxEc800At_Submit(&at,payload ? "AT+QMTPUBEX=0,0,0,0,\"a\",7" : "AT",
            20,ABOX_EC800_PRIORITY_NORMAL,1000,tx_failed,&completions[0]));
        if (payload) ABoxEc800At_Task(&at);
        assert(ABoxEc800At_Submit(&at,"AT",21,ABOX_EC800_PRIORITY_NORMAL,1000,tx_failed,&completions[1]));
        if (payload) ABoxEc800At_Feed(&at,(const uint8_t *)"> ",2);
        else { fail_write=1; ABoxEc800At_Task(&at); }
        assert(at.quarantined && !ABoxEc800At_IsBusy(&at));
        assert(completions[0]==1 && completions[1]==1);
        ABoxEc800At_Feed(&at,(const uint8_t *)"OK\r\n",4);
        ABoxEc800At_Task(&at); ABoxEc800At_Quarantine(&at);
        assert(completions[0]==1 && completions[1]==1);
        assert(!ABoxEc800At_Submit(&at,"AT",20,ABOX_EC800_PRIORITY_NORMAL,1000,done,NULL));
        ABoxEc800At_OnModemReset(&at); fail_write=0;
        assert(ABoxEc800At_Submit(&at,"AT",20,ABOX_EC800_PRIORITY_NORMAL,1000,done,NULL));
        g_done_count=0; ABoxEc800At_Task(&at);
        ABoxEc800At_Feed(&at,(const uint8_t *)"OK\r\n",4);
        assert(g_done_count==1 && g_done_result==ABOX_EC800_RESULT_OK);
    }
}

int main(void)
{
    test_bulk_termination();
    test_failed_write_terminates_session();
    test_raw_reset_does_not_underflow();
    test_credentials_never_reach_line_observers();
    test_async_direct_command();
    test_http_post_waits_for_payload_and_result_urc();
    test_raw_split_and_dynamic_owner();
    test_owner_routing_and_raw_urc_glue();
    test_dma_half_full_and_wrap();
    test_circular_dma_wrap_and_overflow();
    test_internal_mqtt_max_payload_line_is_delivered();
    test_overlong_line_is_dropped_and_next_urc_recovers();
    test_modem_ready_reaches_all_owners_during_active_command();
    return 0;
}
