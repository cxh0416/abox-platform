#include "abox_mqtt_ec800.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(check) do { if (!(check)) { \
    fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #check); \
    exit(1); \
} } while (0)

typedef struct {
    ABoxEc800At at;
    ABoxMqttEc800 mqtt;
    uint32_t now;
    char command[256];
    uint8_t sent_payload[64];
    size_t sent_length;
    unsigned messages, submitted, confirmed, failed, resets;
    int fail_write;
    uint64_t last_operation;
} Fixture;

static uint32_t tick(void *context)
{
    return ((Fixture *)context)->now;
}
static int write_bytes(void *context, const uint8_t *data, uint16_t length,
                       uint32_t timeout)
{
    Fixture *f = (Fixture *)context;
    (void)timeout;
    if (f->fail_write) return 0;
    if (length >= 3U && data[0] == 'A' && data[1] == 'T') {
        assert(length < sizeof(f->command));
        memcpy(f->command, data, length);
        f->command[length] = 0;
    } else {
        assert(length <= sizeof(f->sent_payload));
        memcpy(f->sent_payload, data, length);
        f->sent_length = length;
    }
    return 1;
}
static void incoming(void *user, const uint8_t *topic, size_t topic_length,
                     const uint8_t *payload, size_t payload_length)
{
    Fixture *f = (Fixture *)user;
    assert(topic_length == 3U && !memcmp(topic, "a/b", 3U));
    assert(payload_length == 4U && !memcmp(payload, "test", 4U));
    ++f->messages;
}
static void published(void *user, uint64_t operation,
                      ABoxMqttEc800PublishEvent event)
{
    Fixture *f = (Fixture *)user;
    f->last_operation = operation;
    if (event == ABOX_MQTT_EC800_PUBLISH_SUBMITTED) ++f->submitted;
    if (event == ABOX_MQTT_EC800_PUBLISH_CONFIRMED) ++f->confirmed;
    if (event == ABOX_MQTT_EC800_PUBLISH_FAILED) ++f->failed;
}
static void modem_reset(void *user)
{ ++((Fixture *)user)->resets; }
static void feed(Fixture *f, const char *line)
{
    ABoxEc800At_Feed(&f->at, (const uint8_t *)line, (uint16_t)strlen(line));
}
static void cycle(Fixture *f)
{
    ABoxMqttEc800_Poll(&f->mqtt, f->now);
    ABoxEc800At_Task(&f->at);
}
static void answer(Fixture *f, const char *expected, const char *response)
{
    cycle(f);
    if (strncmp(f->command, expected, strlen(expected)))
        fprintf(stderr, "expected %s, received %s\n", expected, f->command);
    assert(strncmp(f->command, expected, strlen(expected)) == 0);
    f->command[0] = 0;
    feed(f, response);
    ++f->now;
    cycle(f);
}
static void connect_session(Fixture *f)
{
    char sub_response[48];
    answer(f, "ATE0", "OK\r\n");
    answer(f, "AT+QMTCFG", "OK\r\n");
    answer(f, "AT+QMTCFG=\"version\",0,4", "OK\r\n");
    answer(f, "AT+QMTCFG=\"pdpcid\",0,1", "OK\r\n");
    if (f->mqtt.config.clean_session)
        answer(f, "AT+QMTCFG=\"session\",0,1", "OK\r\n");
    else cycle(f); /* Skip the optional configuration without changing legacy settings. */
    answer(f, "AT+CEREG?", "+CEREG: 0,1\r\nOK\r\n");
    answer(f, "AT+CGATT?", "+CGATT: 1\r\nOK\r\n");
    answer(f, "AT+QIACT?", "+QIACT: 1,1,1\r\nOK\r\n");
    answer(f, "AT+QMTOPEN", "OK\r\n+QMTOPEN: 0,0\r\n");
    answer(f, "AT+QMTCONN", "OK\r\n+QMTCONN: 0,0,0\r\n");
    cycle(f);
    assert(strncmp(f->command, "AT+QMTSUB", 9U) == 0);
    snprintf(sub_response, sizeof(sub_response), "OK\r\n+QMTSUB: 0,%u,0,1\r\n",
             f->mqtt.pending_sub_msg_id);
    f->command[0] = 0;
    feed(f, sub_response);
    ++f->now;
    cycle(f);
    cycle(f);
    assert(ABoxMqttEc800_IsReady(&f->mqtt));
}

static void test_queue_retry(const ABoxMqttEc800Config *config,
                             const ABoxMqttEc800Buffers *buffers)
{
    const ABoxMqttEc800State phases[]={ABOX_MQTT_EC800_CONFIGURING,
        ABOX_MQTT_EC800_OPENING,ABOX_MQTT_EC800_CONNECTING,ABOX_MQTT_EC800_SUBSCRIBING};
    for (unsigned phase=0;phase<4;++phase) for (unsigned expire=0;expire<2;++expire) {
        Fixture f={0};
        ABoxEc800AtPort port={.context=&f,.tick_ms=tick,.write=write_bytes};
        ABoxMqttEc800Callbacks callbacks={.user=&f,.message=incoming};
        uint32_t timeout=phase==0 ? config->command_timeout_ms : config->open_timeout_ms;
        f.now=0xFFFFFF00U;
        assert(ABoxEc800At_Init(&f.at,&port));
        assert(ABoxMqttEc800_Init(&f.mqtt,&f.at,config,buffers,&callbacks,f.now));
        ABoxMqttEc800_SetSecurityReady(&f.mqtt,1);
        assert(ABoxMqttEc800_Start(&f.mqtt,f.now));
        f.mqtt.state=phases[phase];
        for (unsigned i=0;i<ABOX_EC800_AT_QUEUE_SIZE;++i)
            assert(ABoxEc800At_Submit(&f.at,"AT",(ABoxEc800Owner)(20+i),ABOX_EC800_PRIORITY_NORMAL,20000,NULL,NULL));
        for (unsigned i=0;i<3;++i) ABoxMqttEc800_Poll(&f.mqtt,f.now);
        assert(!f.mqtt.command_pending && !f.mqtt.next_sub_msg_id && !f.mqtt.pending_sub_msg_id);
        f.now+=expire ? timeout : timeout-1;
        ABoxEc800At_CancelQueued(&f.at,25);
        ABoxMqttEc800_Poll(&f.mqtt,f.now);
        if (expire) {
            assert(!ABoxEc800At_HasPending(&f.at,ABOX_EC800_OWNER_MQTT));
            assert(f.mqtt.state==(phase ? ABOX_MQTT_EC800_CLOSING : ABOX_MQTT_EC800_RETRY_WAIT));
        } else {
            assert(f.mqtt.command_pending && f.mqtt.state==phases[phase]);
            /* Delay wire start longer than the phase timeout. Queue admission
             * must not immediately cancel the just-accepted command. */
            f.now+=timeout+1;
            ABoxMqttEc800_Poll(&f.mqtt,f.now);
            assert(f.mqtt.state==phases[phase] && !f.at.quarantined);
            ABoxEc800At_Task(&f.at);
            assert(ABoxEc800At_IsActive(&f.at,ABOX_EC800_OWNER_MQTT));
            ABoxMqttEc800_Poll(&f.mqtt,f.now);
            assert(f.mqtt.state==phases[phase]);
            if (phase) {
                f.now+=timeout-1;
                feed(&f,phase==1 ? "OK\r\n+QMTOPEN: 0,0\r\n" :
                    phase==2 ? "OK\r\n+QMTCONN: 0,0,0\r\n" : "OK\r\n+QMTSUB: 0,1,0,1\r\n");
                ABoxMqttEc800_Poll(&f.mqtt,f.now);
                assert(f.mqtt.state!=ABOX_MQTT_EC800_CLOSING && !f.at.quarantined);
            }
        }
    }
}

int main(void)
{
    Fixture f;
    ABoxEc800AtPort port;
    ABoxMqttEc800Config config;
    ABoxMqttEc800Buffers buffers;
    ABoxMqttEc800Callbacks callbacks;
    ABoxMqttEc800Counters counters;
    static uint8_t header[128], topic[128], payload[128], outgoing[64];
    static const char *subscriptions[] = {"a/b"};
    uint64_t operation = 0U;
    size_t capacity = 0U;
    memset(&f, 0, sizeof(f));
    memset(&port, 0, sizeof(port));
    memset(&config, 0, sizeof(config));
    memset(&buffers, 0, sizeof(buffers));
    memset(&callbacks, 0, sizeof(callbacks));
    port.context = &f; port.tick_ms = tick; port.write = write_bytes;
    assert(ABoxEc800At_Init(&f.at, &port));
    config.apn = "CMNET"; config.host = "broker.example";
    config.client_id = "device"; config.username = "user";
    config.password = "secret"; config.port = 1883U;
    config.subscriptions = subscriptions; config.subscription_count = 1U;
    config.command_timeout_ms = 100U; config.open_timeout_ms = 1000U;
    config.connect_timeout_ms = 1000U; config.subscribe_timeout_ms = 1000U;
    config.publish_timeout_ms = 1000U; config.retry_delay_ms = 100U;
    config.mqtt_version = 4U; config.pdp_context_id = 1U;
    config.clean_session = 1U;
    buffers.header = header; buffers.header_capacity = sizeof(header);
    buffers.topic = topic; buffers.topic_capacity = sizeof(topic);
    buffers.payload = payload; buffers.payload_capacity = sizeof(payload);
    buffers.publish_payload = outgoing; buffers.publish_capacity = sizeof(outgoing);
    test_queue_retry(&config,&buffers);
    callbacks.user = &f; callbacks.message = incoming;
    callbacks.publish_event = published;
    callbacks.modem_reset = modem_reset;
    assert(ABoxMqttEc800_Init(&f.mqtt, &f.at, &config, &buffers, &callbacks, 0U));
    ABoxMqttEc800_SetSecurityReady(&f.mqtt, 1U);
    assert(ABoxMqttEc800_Start(&f.mqtt, 0U));
    connect_session(&f);
    feed(&f, "+QMTRECV: 0,0,\"a/b\",4,test\r\n");
    assert(f.messages == 1U);
    assert(ABoxMqttEc800_Publish(&f.mqtt, "a/b",
                                 (const uint8_t *)"hello", 5U, 1U, 0U,
                                 &operation));
    cycle(&f);
    assert(!strncmp(f.command, "AT+QMTPUBEX", 11U));
    feed(&f, "> ");
    assert(f.submitted == 1U && f.sent_length == 5U);
    assert(!memcmp(f.sent_payload, "hello", 5U));
    feed(&f, "\r\n+QMTPUBEX: 0,1,0\r\n");
    assert(f.confirmed == 1U && f.last_operation == operation);
    assert(ABoxMqttEc800_Publish(&f.mqtt, "a/b",
                                 (const uint8_t *)"pulse", 5U, 0U, 0U,
                                 &operation));
    cycle(&f);
    assert(!strncmp(f.command, "AT+QMTPUBEX=0,0,0,0,\"a/b\",5", 27U));
    feed(&f, "> ");
    assert(f.submitted == 2U);
    feed(&f, "\r\n+QMTPUBEX: 0,0,0\r\n");
    assert(f.confirmed == 2U && f.last_operation == operation);
    assert(ABoxMqttEc800_Publish(&f.mqtt, "a/b",
                                 (const uint8_t *)"again", 5U, 1U, 0U,
                                 &operation));
    cycle(&f);
    assert(!strncmp(f.command, "AT+QMTPUBEX=0,2,1,0,\"a/b\",5", 27U));
    feed(&f, "> ");
    feed(&f, "\r\n+QMTPUBEX: 0,2,0\r\n");
    assert(f.confirmed == 3U && f.last_operation == operation);
    feed(&f, "+QMTPUBEX: 0,1,0\r\n");
    ABoxMqttEc800_GetCounters(&f.mqtt, &counters);
    assert(counters.stale_publish == 1U);
    assert(ABoxMqttEc800_Pause(&f.mqtt, f.now) == 0);
    answer(&f, "AT+QMTDISC", "+QMTDISC: 0,0\r\n");
    answer(&f, "AT+QMTCLOSE", "+QMTCLOSE: 0,0\r\n");
    assert(ABoxMqttEc800_Pause(&f.mqtt, f.now) == 0);
    f.now += 1500U;
    cycle(&f);
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_PAUSED);
    assert(ABoxMqttEc800_BorrowWorkspace(&f.mqtt, &capacity) == payload);
    assert(capacity == sizeof(payload));
    assert(ABoxMqttEc800_ReturnWorkspace(&f.mqtt));
    assert(ABoxMqttEc800_Configure(&f.mqtt, &config));
    assert(ABoxMqttEc800_Resume(&f.mqtt, f.now));
    connect_session(&f);
    feed(&f, "+QMTSTAT: 0,1\r\n");
    cycle(&f);
    answer(&f, "AT+QMTDISC", "+QMTDISC: 0,0\r\n");
    answer(&f, "AT+QMTCLOSE", "+QMTCLOSE: 0,0\r\n");
    cycle(&f);
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_RETRY_WAIT);
    feed(&f, "+QMTPUBEX: 0,1,0\r\n");
    f.now += 1500U;
    cycle(&f);
    connect_session(&f);
    ABoxMqttEc800_GetCounters(&f.mqtt, &counters);
    assert(counters.reconnects == 1U);
    assert(ABoxMqttEc800_Publish(&f.mqtt, "a/b",
                                 (const uint8_t *)"late", 4U, 1U, 0U,
                                 &operation));
    cycle(&f);
    f.now += 1001U;
    cycle(&f);
    assert(f.failed == 1U);
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_BLOCKED);
    assert(!ABoxMqttEc800_IsReady(&f.mqtt));
    ABoxMqttEc800_GetCounters(&f.mqtt, &counters);
    assert(counters.publish_timeout == 1U);
    ABoxEc800At_Reset(&f.at);
    ABoxMqttEc800_OnModemReset(&f.mqtt, f.now);
    connect_session(&f);
    feed(&f, "RDY\r\n");
    assert(f.resets == 1U);
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_CONFIGURING);
    connect_session(&f);
    memset(&f, 0, sizeof(f));
    assert(ABoxEc800At_Init(&f.at, &port));
    assert(ABoxMqttEc800_Init(&f.mqtt, &f.at, &config, &buffers, &callbacks, 0U));
    ABoxMqttEc800_SetSecurityReady(&f.mqtt, 1U);
    assert(ABoxMqttEc800_Start(&f.mqtt, 0U));
    answer(&f, "ATE0", "OK\r\n");
    answer(&f, "AT+QMTCFG", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"version\",0,4", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"pdpcid\",0,1", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"session\",0,1", "OK\r\n");
    answer(&f, "AT+CEREG?", "+CEREG: 0,1\r\nOK\r\n");
    answer(&f, "AT+CGATT?", "+CGATT: 1\r\nOK\r\n");
    answer(&f, "AT+QIACT?", "+QIACT: 1,1,1\r\nOK\r\n");
    answer(&f, "AT+QMTOPEN", "OK\r\n+QMTOPEN: 0,0\r\n");
    answer(&f, "AT+QMTCONN", "OK\r\n+QMTCONN: 0,0,0\r\n");
    answer(&f, "AT+QMTSUB", "OK\r\n+QMTSUB: 0,1,2\r\n");
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_CLOSING);
    answer(&f, "AT+QMTDISC", "+QMTDISC: 0,0\r\n");
    answer(&f, "AT+QMTCLOSE", "+QMTCLOSE: 0,0\r\n");
    assert(!ABoxMqttEc800_IsReady(&f.mqtt));
    memset(&f, 0, sizeof(f));
    assert(ABoxEc800At_Init(&f.at, &port));
    assert(ABoxMqttEc800_Init(&f.mqtt, &f.at, &config, &buffers, &callbacks, 0U));
    ABoxMqttEc800_SetSecurityReady(&f.mqtt, 1U);
    assert(ABoxMqttEc800_Start(&f.mqtt, 0U));
    cycle(&f);
    assert(!strncmp(f.command, "ATE0", 4U));
    assert(ABoxMqttEc800_Stop(&f.mqtt, f.now) == 0);
    assert(ABoxMqttEc800_GetState(&f.mqtt) != ABOX_MQTT_EC800_BLOCKED);
    feed(&f, "OK\r\n");
    cycle(&f);
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_RETRY_WAIT);
    assert(!strncmp(f.command, "ATE0", 4U));
    f.now += 1500U;
    cycle(&f);
    assert(ABoxMqttEc800_Stop(&f.mqtt, f.now) == 1);
    memset(&f, 0, sizeof(f));
    assert(ABoxEc800At_Init(&f.at, &port));
    assert(ABoxMqttEc800_Init(&f.mqtt, &f.at, &config, &buffers, &callbacks, 0U));
    ABoxMqttEc800_SetSecurityReady(&f.mqtt, 1U);
    assert(ABoxMqttEc800_Start(&f.mqtt, 0U));
    answer(&f, "ATE0", "OK\r\n");
    answer(&f, "AT+QMTCFG", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"version\",0,4", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"pdpcid\",0,1", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"session\",0,1", "OK\r\n");
    answer(&f, "AT+CEREG?", "+CEREG: 0,2\r\nOK\r\n");
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_RETRY_WAIT);
    assert(f.command[0] == 0);
    f.now += 1601U;
    cycle(&f);
    connect_session(&f);
    assert(ABoxMqttEc800_IsReady(&f.mqtt));
    assert(ABoxMqttEc800_Publish(&f.mqtt, "a/b",
                                 (const uint8_t *)"probe", 5U, 1U, 0U,
                                 &operation));
    cycle(&f);
    feed(&f, "> ");
    feed(&f, "\r\n+QMTPUBEX: 0,1,2\r\n");
    assert(f.failed == 1U);
    cycle(&f);
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_CLOSING);
    answer(&f, "AT+QMTDISC", "+QMTDISC: 0,0\r\n");
    answer(&f, "AT+QMTCLOSE", "+QMTCLOSE: 0,0\r\n");
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_RETRY_WAIT);
    memset(&f, 0, sizeof(f));
    config.client_index = 1U;
    config.host = "broker.invalid";
    assert(ABoxEc800At_Init(&f.at, &port));
    assert(ABoxMqttEc800_Init(&f.mqtt, &f.at, &config, &buffers, &callbacks, 0U));
    ABoxMqttEc800_SetSecurityReady(&f.mqtt, 1U);
    assert(ABoxMqttEc800_Start(&f.mqtt, 0U));
    answer(&f, "ATE0", "OK\r\n");
    answer(&f, "AT+QMTCFG", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"version\",1,4", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"pdpcid\",1,1", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"session\",1,1", "OK\r\n");
    answer(&f, "AT+CEREG?", "+CEREG: 0,1\r\nOK\r\n");
    answer(&f, "AT+CGATT?", "+CGATT: 1\r\nOK\r\n");
    answer(&f, "AT+QIACT?", "+QIACT: 1,1,1\r\nOK\r\n");
    answer(&f, "AT+QMTOPEN=1", "OK\r\n+QMTOPEN: 1,-1\r\n");
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_RETRY_WAIT);
    assert(f.command[0] == 0 && !f.at.quarantined);
    assert(ABoxMqttEc800_Stop(&f.mqtt, f.now) == 0);
    f.now += 1601U;
    cycle(&f);
    assert(ABoxMqttEc800_Stop(&f.mqtt, f.now) == 1);
    assert(f.command[0] == 0 && !f.at.quarantined);
    memset(&f, 0, sizeof(f));
    assert(ABoxEc800At_Init(&f.at, &port));
    assert(ABoxMqttEc800_Init(&f.mqtt, &f.at, &config, &buffers, &callbacks, 0U));
    ABoxMqttEc800_SetSecurityReady(&f.mqtt, 1U);
    assert(ABoxMqttEc800_Start(&f.mqtt, 0U));
    answer(&f, "ATE0", "OK\r\n");
    answer(&f, "AT+QMTCFG", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"version\",1,4", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"pdpcid\",1,1", "OK\r\n");
    answer(&f, "AT+QMTCFG=\"session\",1,1", "OK\r\n");
    answer(&f, "AT+CEREG?", "+CEREG: 0,1\r\nOK\r\n");
    answer(&f, "AT+CGATT?", "+CGATT: 1\r\nOK\r\n");
    answer(&f, "AT+QIACT?", "+QIACT: 1,1,1\r\nOK\r\n");
    answer(&f, "AT+QMTOPEN=1", "OK\r\n+QMTOPEN: 1,2\r\n");
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_CLOSING);
    answer(&f, "AT+QMTDISC=1", "ERROR\r\n");
    answer(&f, "AT+QMTCLOSE=1", "ERROR\r\n");
    assert(ABoxMqttEc800_GetState(&f.mqtt) == ABOX_MQTT_EC800_BLOCKED);
    assert(f.at.quarantined);
    /* A confirmed failed TLS open has no client to close. Drain, then stop
     * without sending QMTDISC/QMTCLOSE or weakening security readiness. */
    memset(&f, 0, sizeof(f));
    assert(ABoxEc800At_Init(&f.at, &port));
    assert(ABoxMqttEc800_Init(&f.mqtt, &f.at, &config, &buffers, &callbacks, 0U));
    f.mqtt.state = ABOX_MQTT_EC800_OPENING;
    f.mqtt.enabled = f.mqtt.security_ready = 1;
    cycle(&f); /* QMTOPEN submitted, only terminal OK arrived so far. */
    feed(&f, "OK\r\n");
    f.command[0] = 0;
    assert(ABoxMqttEc800_Stop(&f.mqtt, f.now) == 0);
    cycle(&f);
    assert(f.mqtt.state == ABOX_MQTT_EC800_OPENING && !f.command[0]);
    feed(&f, "+QMTOPEN: 1,-1\r\n");
    cycle(&f);
    assert(f.mqtt.state == ABOX_MQTT_EC800_RETRY_WAIT);
    assert(!f.at.quarantined && !f.command[0]);
    assert(ABoxMqttEc800_Stop(&f.mqtt, f.now) == 0);
    f.now += 1600; cycle(&f);
    assert(ABoxMqttEc800_Stop(&f.mqtt, f.now) == 1);
    /* Broker CONNACK 5: TLS succeeded, MQTT was refused and socket closed.
     * A modem ERROR from closing that rejected client is synchronized. */
    memset(&f,0,sizeof(f));
    assert(ABoxEc800At_Init(&f.at,&port));
    assert(ABoxMqttEc800_Init(&f.mqtt,&f.at,&config,&buffers,&callbacks,0U));
    f.mqtt.state=ABOX_MQTT_EC800_CONNECTING;
    f.mqtt.enabled=f.mqtt.security_ready=1;
    answer(&f,"AT+QMTCONN=1","OK\r\n+QMTCONN: 1,0,5\r\n");
    assert(f.mqtt.state==ABOX_MQTT_EC800_CLOSING);
    answer(&f,"AT+QMTCLOSE=1","ERROR\r\n");
    assert(f.mqtt.state==ABOX_MQTT_EC800_RETRY_WAIT && !f.at.quarantined);
    assert(ABoxMqttEc800_Stop(&f.mqtt,f.now)==0);
    f.now+=1600; cycle(&f);
    assert(ABoxMqttEc800_Stop(&f.mqtt,f.now)==1);
    for (unsigned mode=0; mode<4; ++mode) for (unsigned stage=0; stage<3; ++stage) {
        memset(&f,0,sizeof(f));
        assert(ABoxEc800At_Init(&f.at,&port));
        assert(ABoxMqttEc800_Init(&f.mqtt,&f.at,&config,&buffers,&callbacks,0));
        f.mqtt.state=ABOX_MQTT_EC800_READY;
        f.mqtt.enabled=f.mqtt.security_ready=1;
        assert(ABoxMqttEc800_Publish(&f.mqtt,"a/b",(const uint8_t *)"test",4,1,0,&operation));
        if (stage) ABoxEc800At_Task(&f.at);
        if (stage==2) feed(&f,"> \r\nSEND OK\r\n"); /* AT done, publish ACK outstanding */
        for (unsigned duplicate=0; duplicate<2; ++duplicate) {
            if (!mode) feed(&f,"RDY\r\n");
            else if (mode==1) ABoxEc800At_AbortAll(&f.at,ABOX_EC800_RESULT_ERROR);
            else if (mode==2) ABoxEc800At_Quarantine(&f.at);
            else ABoxMqttEc800_SetSecurityReady(&f.mqtt,0);
            if (mode==3) assert(!f.mqtt.publish_active); /* safety gate does not depend on Poll */
            ABoxMqttEc800_Poll(&f.mqtt,f.now);
        }
        assert(!f.mqtt.publish_active && f.failed==1 && !f.confirmed);
        ABoxEc800At_OnModemReset(&f.at);
        f.mqtt.state=ABOX_MQTT_EC800_READY;
        ABoxMqttEc800_SetSecurityReady(&f.mqtt,1);
        assert(ABoxMqttEc800_Publish(&f.mqtt,"a/b",(const uint8_t *)"test",4,1,0,&operation));
        ABoxEc800At_Task(&f.at); feed(&f,"> \r\n");
        feed(&f,"+QMTPUBEX: 1,1,0\r\n"); /* old publish must not complete new session */
        assert(!f.confirmed && f.mqtt.publish_active);
        feed(&f,"+QMTPUBEX: 1,2,0\r\n");
        assert(f.confirmed==1 && !f.mqtt.publish_active && f.failed==1);
    }
    /* Payload write invokes done synchronously inside the prompt observer. */
    memset(&f,0,sizeof(f));
    assert(ABoxEc800At_Init(&f.at,&port));
    assert(ABoxMqttEc800_Init(&f.mqtt,&f.at,&config,&buffers,&callbacks,0));
    f.mqtt.state=ABOX_MQTT_EC800_READY;
    f.mqtt.enabled=f.mqtt.security_ready=1;
    assert(ABoxMqttEc800_Publish(&f.mqtt,"a/b",(const uint8_t *)"test",4,1,0,&operation));
    ABoxEc800At_Task(&f.at); f.fail_write=1; feed(&f,"> ");
    assert(f.failed==1 && !f.submitted && !f.confirmed && !f.mqtt.publish_active && f.at.quarantined);
    feed(&f,"OK\r\n+QMTPUBEX: 1,1,0\r\n");
    ABoxMqttEc800_Poll(&f.mqtt,f.now);
    assert(f.failed==1 && !f.confirmed && !ABoxEc800At_IsBusy(&f.at));
    /* Legacy consumers leave the optional session command untouched. */
    memset(&f,0,sizeof(f)); config.client_index=0; config.clean_session=0;
    assert(ABoxEc800At_Init(&f.at,&port));
    assert(ABoxMqttEc800_Init(&f.mqtt,&f.at,&config,&buffers,&callbacks,0));
    ABoxMqttEc800_SetSecurityReady(&f.mqtt,1);
    assert(ABoxMqttEc800_Start(&f.mqtt,0));
    connect_session(&f);
    puts("abox_mqtt_ec800_test passed");
    return 0;
}
