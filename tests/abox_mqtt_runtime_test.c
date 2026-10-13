#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "abox_mqtt_runtime.h"

typedef struct {
    uint32_t now;
    uint8_t paused, security, connected, ready;
    unsigned writes, revokes, transport_stops, modem_resets;
    uint8_t reconfig_required;
    int transport_stop_result;
    char last_command[128];
} Fixture;

static uint32_t tick(void *context) { return ((Fixture *)context)->now; }
static int write_at(void *context, const uint8_t *data, uint16_t length,
                    uint32_t timeout)
{
    Fixture *f = context;
    (void)timeout;
    assert(length < sizeof(f->last_command));
    memcpy(f->last_command, data, length);
    f->last_command[length] = '\0';
    ++f->writes;
    return 1;
}
static void set_paused(void *context, uint8_t value) { ((Fixture *)context)->paused = value; }
static void set_security(void *context, uint8_t value) { ((Fixture *)context)->security = value; }
static int apply(void *context, const ABoxMqttConfig *config)
{ return !((Fixture *)context)->reconfig_required && config && config->host && config->port; }
static void mqtt_task(void *context) { (void)context; }
static void transport_modem_reset(void *context)
{
    Fixture *f = context;
    assert(f->paused && !f->security);
    ++f->modem_resets;
    f->reconfig_required = 0U;
}
static int transport_stop(void *context, uint32_t now)
{
    Fixture *f = context;
    (void)now;
    ++f->transport_stops;
    return f->transport_stop_result;
}
static uint8_t connected(void *context) { return ((Fixture *)context)->connected; }
static uint8_t ready(void *context) { return ((Fixture *)context)->ready; }
static uint8_t clock_invalid(void *context) { (void)context; return 0U; }
static uint8_t yes(void *context) { (void)context; return 1U; }
static void revoke(void *context) { ++((Fixture *)context)->revokes; }

int main(void)
{
    ABoxMqttRuntimeOptions standard;
    ABoxMqttRuntimeOptions_StandardEc800(&standard, 20U, 0U);
    assert(standard.owner == 20U && standard.tls_profile_id == 0U);
    assert(standard.plain_client == 0U && standard.tls_client == 1U);
    assert(standard.tls_context == 2U && standard.supported_tls_context_mask == 4U);
    assert(standard.command_timeout_ms == 5000U &&
           standard.tls_timeout_ms == 30000U &&
           standard.connect_timeout_ms == 60000U &&
           standard.subscribe_timeout_ms == 60000U);
    assert(strcmp(standard.ca_file, "UFS:ota_ca.pem") == 0);
    assert(standard.ca_ciphersuite == 0U);
    Fixture f = {0};
    ABoxEc800At at;
    ABoxEc800AtPort at_port = {0};
    ABoxMqttRuntime runtime;
    ABoxMqttRuntimePort port = {0};
    ABoxMqttRuntimeOptions options = {0};
    const ABoxMqttConfig first = {"first.example", 1883U, "u", "p", 0U, 0U};
    const ABoxMqttConfig next = {"next.example", 1884U, "u", "p", 0U, 0U};
    const uint8_t disc[] = "OK\r\n+QMTDISC: 0,0\r\n";
    const uint8_t close[] = "OK\r\n+QMTCLOSE: 0,0\r\n";
    const uint8_t disc_tls[] = "OK\r\n+QMTDISC: 1,0\r\n";
    const uint8_t close_tls[] = "OK\r\n+QMTCLOSE: 1,0\r\n";

    at_port.context = &f; at_port.tick_ms = tick; at_port.write = write_at;
    assert(ABoxEc800At_Init(&at, &at_port));
    port.user = &f; port.set_paused = set_paused; port.set_security_ready = set_security;
    port.apply_config = apply; port.mqtt_task = mqtt_task;
    port.mqtt_ready = ready; port.mqtt_connected = connected;
    port.ca_ready = yes; port.time_valid = yes; port.before_network_change = revoke;
    options.owner = 20U; options.command_timeout_ms = 5000U;
    options.tls_timeout_ms = 30000U; options.connect_timeout_ms = 60000U;
    options.subscribe_timeout_ms = 60000U; options.ca_file = "UFS:ca.pem";
    options.ca_revision = 1U;
    options.plain_client = 0U; options.tls_client = 1U;
    options.tls_context = 2U; options.supported_tls_context_mask = 4U;
    assert(ABoxMqttRuntime_Init(&runtime, &at, &port, &options, &first, 0U));
    assert(runtime.state == ABOX_MQTT_RUNTIME_PREPARED && !f.paused && f.security);
    ABoxMqttRuntime_Poll(&runtime, 1U);
    assert(runtime.state == ABOX_MQTT_RUNTIME_CONNECT);
    f.connected = 1U; ABoxMqttRuntime_Poll(&runtime, 2U);
    f.ready = 1U; ABoxMqttRuntime_Poll(&runtime, 3U);
    assert(ABoxMqttRuntime_IsReady(&runtime));
    assert(strcmp(ABoxMqttRuntime_ActiveConfig(&runtime)->host, "first.example") == 0);
    runtime.callbacks.time_valid = clock_invalid;
    ABoxMqttRuntime_Poll(&runtime, 2002U);
    ABoxEc800At_Task(&at);
    assert(strstr(f.last_command, "AT+CCLK?") != 0);
    ABoxEc800At_Feed(&at, (const uint8_t *)"\r\nOK\r\n", 6U);
    ABoxMqttRuntime_Poll(&runtime, 2003U);
    assert(!runtime.waiting && ABoxMqttRuntime_IsReady(&runtime));
    runtime.callbacks.time_valid = yes;

    runtime.options.query_clock_mode = 1U;
    runtime.callbacks.time_valid = clock_invalid;
    ABoxMqttRuntime_Poll(&runtime, 4003U);
    ABoxEc800At_Task(&at);
    assert(strstr(f.last_command, "AT+CTZU?") != 0);
    ABoxEc800At_Feed(&at, (const uint8_t *)"\r\nOK\r\n", 6U);
    ABoxMqttRuntime_Poll(&runtime, 4004U);
    ABoxEc800At_Task(&at);
    assert(strstr(f.last_command, "AT+CCLK?") != 0);
    ABoxEc800At_Feed(&at, (const uint8_t *)"\r\nOK\r\n", 6U);
    ABoxMqttRuntime_Poll(&runtime, 4005U);
    assert(!runtime.waiting);
    runtime.callbacks.time_valid = yes;


    assert(ABoxMqttRuntime_Stage(&runtime, &next));
    assert(ABoxMqttRuntime_Activate(&runtime, 4U));
    assert(f.paused && !f.security && f.revokes == 1U);
    ABoxEc800At_Task(&at);
    assert(strstr(f.last_command, "AT+QMTDISC=0") != 0);
    ABoxEc800At_Feed(&at, disc, (uint16_t)(sizeof(disc) - 1U));
    ABoxMqttRuntime_Poll(&runtime, 5U);
    ABoxMqttRuntime_Poll(&runtime, 6U);
    ABoxEc800At_Task(&at);
    assert(strstr(f.last_command, "AT+QMTCLOSE=0") != 0);
    ABoxEc800At_Feed(&at, close, (uint16_t)(sizeof(close) - 1U));
    ABoxMqttRuntime_Poll(&runtime, 7U);
    assert(runtime.state == ABOX_MQTT_RUNTIME_PREPARED);
    f.connected = f.ready = 0U;
    ABoxMqttRuntime_Poll(&runtime, 8U);
    f.connected = 1U; ABoxMqttRuntime_Poll(&runtime, 9U);
    f.ready = 1U; ABoxMqttRuntime_Poll(&runtime, 10U);
    assert(ABoxMqttRuntime_IsReady(&runtime));
    assert(strcmp(ABoxMqttRuntime_ActiveConfig(&runtime)->host, "next.example") == 0);
    assert(ABoxEc800At_Submit(&at, "AT+CSQ", 30U, ABOX_EC800_PRIORITY_NORMAL,
                             5000U, 0, 0));
    ABoxEc800At_Task(&at);
    assert(ABoxMqttRuntime_StopFirst(&runtime, 11U) == 0);
    assert(f.paused && !f.security && f.revokes == 2U);
    ABoxMqttRuntime_Poll(&runtime, 12U);
    assert(ABoxMqttRuntime_GetState(&runtime) == ABOX_MQTT_RUNTIME_STOP_WAIT_DRAIN);
    ABoxEc800At_Feed(&at, (const uint8_t *)"OK\r\n", 4U);
    ABoxMqttRuntime_Poll(&runtime, 13U);
    assert(ABoxMqttRuntime_GetState(&runtime) == ABOX_MQTT_RUNTIME_STOP_DISCONNECT);
    ABoxMqttRuntime_Poll(&runtime, 14U);
    ABoxEc800At_Task(&at);
    assert(strstr(f.last_command, "AT+QMTDISC=0") != 0);
    ABoxEc800At_Feed(&at, disc, (uint16_t)(sizeof(disc) - 1U));
    ABoxMqttRuntime_Poll(&runtime, 15U);
    ABoxMqttRuntime_Poll(&runtime, 16U);
    ABoxEc800At_Task(&at);
    assert(strstr(f.last_command, "AT+QMTCLOSE=0") != 0);
    ABoxEc800At_Feed(&at, close, (uint16_t)(sizeof(close) - 1U));
    ABoxMqttRuntime_Poll(&runtime, 17U);
    assert(ABoxMqttRuntime_StopFirst(&runtime, 18U) == 1);
    assert(ABoxMqttRuntime_GetState(&runtime) == ABOX_MQTT_RUNTIME_UNENROLLED);
    assert(ABoxMqttRuntime_ActiveConfig(&runtime) == 0);
    ABoxMqttRuntime_Block(&runtime);
    assert(!ABoxMqttRuntime_Stage(&runtime, &first));
    assert(ABoxMqttRuntime_RecoveryComplete(&runtime));
    assert(ABoxMqttRuntime_Stage(&runtime, &first));

    {
        Fixture tls_fixture = {0};
        ABoxEc800At tls_at;
        ABoxMqttRuntime tls_runtime;
        const ABoxMqttConfig tls_config = {"secure.example", 8883U, "u", "p", 1U, 0U};
        uint32_t now;
        at_port.context = &tls_fixture;
        port.user = &tls_fixture;
        options.ca_ciphersuite = 0xC02B;
        assert(ABoxEc800At_Init(&tls_at, &at_port));
        assert(ABoxMqttRuntime_Init(&tls_runtime, &tls_at, &port, &options,
                                    &tls_config, 0U));
        for (now = 1U; now < 80U && !ABoxMqttRuntime_IsReady(&tls_runtime); ++now) {
            tls_fixture.now = now;
            if (tls_runtime.state == ABOX_MQTT_RUNTIME_CONNECT ||
                tls_runtime.state == ABOX_MQTT_RUNTIME_CONNECTED) {
                tls_fixture.connected = tls_fixture.ready = 1U;
            }
            ABoxMqttRuntime_Poll(&tls_runtime, now);
            ABoxEc800At_Task(&tls_at);
            if (tls_at.active_valid) {
                const uint8_t ok[] = "OK\r\n";
                ABoxEc800At_Feed(&tls_at, ok, (uint16_t)(sizeof(ok) - 1U));
            }
        }
        assert(ABoxMqttRuntime_IsReady(&tls_runtime));
        assert(ABoxMqttRuntime_IsTlsActive(&tls_runtime));
        assert(tls_runtime.lease.generation != 0U);
        assert(tls_runtime.tls.slots[2].ca_ciphersuite == 0xC02B);
        assert(ABoxMqttRuntime_StopFirst(&tls_runtime, now++) == 0);
        for (; now < 120U && ABoxMqttRuntime_StopFirst(&tls_runtime, now) == 0; ++now) {
            tls_fixture.now = now;
            ABoxMqttRuntime_Poll(&tls_runtime, now);
            ABoxEc800At_Task(&tls_at);
            if (tls_at.active_valid) {
                const uint8_t ok[] = "OK\r\n";
                if (strstr(tls_fixture.last_command, "AT+QMTDISC=1"))
                    ABoxEc800At_Feed(&tls_at, disc_tls, (uint16_t)(sizeof(disc_tls) - 1U));
                else if (strstr(tls_fixture.last_command, "AT+QMTCLOSE=1"))
                    ABoxEc800At_Feed(&tls_at, close_tls, (uint16_t)(sizeof(close_tls) - 1U));
                else
                    ABoxEc800At_Feed(&tls_at, ok, (uint16_t)(sizeof(ok) - 1U));
            }
        }
        assert(ABoxMqttRuntime_StopFirst(&tls_runtime, now) == 1);
        assert(!ABoxMqttRuntime_IsTlsActive(&tls_runtime));
        assert(tls_runtime.lease.generation == 0U);
    }
    {
        Fixture ps = {0};
        ABoxEc800At ps_at;
        ABoxMqttRuntime ps_runtime;
        ABoxMqttConfig secure = {"psk.example", 26443, "u", "p", 1, 0};
        unsigned step;
        secure.tls_credentials.mode = ABOX_TLS_AUTH_PSK;
        secure.tls_credentials.generation = 2;
        strcpy(secure.tls_credentials.identity, "fixture-g2");
        strcpy(secure.tls_credentials.secret, "abcdefghijklmnopqrstuvwxyz012345");
        at_port.context = &ps;
        port.user = &ps;
        port.ca_ready = port.time_valid = clock_invalid;
        assert(ABoxEc800At_Init(&ps_at, &at_port));
        assert(ABoxMqttRuntime_Init(&ps_runtime, &ps_at, &port, &options, &secure, 0));
        for (step = 1; step < 40 && ps_runtime.state != ABOX_MQTT_RUNTIME_CONNECT; ++step) {
            ps.now = step;
            ABoxMqttRuntime_Poll(&ps_runtime, step);
            ABoxEc800At_Task(&ps_at);
            assert(!strstr(ps.last_command, "CCLK") && !strstr(ps.last_command, "cacert"));
            if (ps_at.active_valid) ABoxEc800At_Feed(&ps_at, (const uint8_t *)"OK\r\n", 4);
        }
        assert(ABoxMqttRuntime_IsTlsActive(&ps_runtime));
        assert(!ABoxMqttRuntime_IsReady(&ps_runtime));
        ps.connected = ps.ready = 1;
        ABoxMqttRuntime_Poll(&ps_runtime, step++);
        ABoxMqttRuntime_Poll(&ps_runtime, step++);
        assert(ABoxMqttRuntime_IsReady(&ps_runtime));
        assert(ps_runtime.active.tls_credentials.generation == 2);
        ABoxMqttRuntime_OnModemReset(&ps_runtime, step);
        assert(ps_runtime.state == ABOX_MQTT_RUNTIME_WAIT_CA);
        assert(ps_runtime.requested.tls_credentials.generation == 2);
        port.ca_ready = port.time_valid = yes;
    }
    {
        Fixture live_reset = {0};
        ABoxEc800At live_at;
        ABoxMqttRuntime live_runtime;
        at_port.context = &live_reset;
        port.user = &live_reset;
        port.mqtt_modem_reset = transport_modem_reset;
        assert(ABoxEc800At_Init(&live_at, &at_port));
        assert(ABoxMqttRuntime_Init(&live_runtime, &live_at, &port, &options,
                                    &first, 0U));
        live_reset.connected = live_reset.ready = 1U;
        ABoxMqttRuntime_Poll(&live_runtime, 1U);
        ABoxMqttRuntime_Poll(&live_runtime, 2U);
        ABoxMqttRuntime_Poll(&live_runtime, 3U);
        assert(ABoxMqttRuntime_IsReady(&live_runtime));
        live_reset.reconfig_required = 1U;
        ABoxEc800At_Feed(&live_at, (const uint8_t *)"RDY\r\n", 5U);
        assert(live_reset.modem_resets == 1U);
        assert(live_runtime.state == ABOX_MQTT_RUNTIME_PREPARED);
        assert(!live_reset.paused && live_reset.security);
        port.mqtt_modem_reset = 0;
    }
    {
        Fixture reset_fixture = {0};
        ABoxEc800At reset_at;
        ABoxMqttRuntime reset_runtime;
        at_port.context = &reset_fixture;
        port.user = &reset_fixture;
        assert(ABoxEc800At_Init(&reset_at, &at_port));
        assert(ABoxMqttRuntime_Init(&reset_runtime, &reset_at, &port, &options,
                                    &first, 0U));
        reset_fixture.connected = reset_fixture.ready = 1U;
        ABoxMqttRuntime_Poll(&reset_runtime, 1U);
        ABoxMqttRuntime_Poll(&reset_runtime, 2U);
        ABoxMqttRuntime_Poll(&reset_runtime, 3U);
        assert(ABoxMqttRuntime_IsReady(&reset_runtime));
        assert(ABoxMqttRuntime_StopFirst(&reset_runtime, 4U) == 0);
        ABoxMqttRuntime_OnModemReset(&reset_runtime, 5U);
        assert(ABoxMqttRuntime_GetState(&reset_runtime) == ABOX_MQTT_RUNTIME_UNENROLLED);
        assert(ABoxMqttRuntime_Stage(&reset_runtime, &next));
        ABoxMqttRuntime_Block(&reset_runtime); /* explicit restore/readback safety latch */
        ABoxEc800At_Feed(&reset_at, (const uint8_t *)"RDY\r\nRDY\r\n", 10U);
        assert(reset_runtime.state == ABOX_MQTT_RUNTIME_BLOCKED && reset_runtime.safety_locked);
        assert(!ABoxMqttRuntime_NeedsModemRecovery(&reset_runtime));
        assert(!ABoxMqttRuntime_Stage(&reset_runtime, &next));
        assert(reset_fixture.paused && !reset_fixture.security);
        assert(ABoxMqttRuntime_RecoveryComplete(&reset_runtime));
        assert(ABoxMqttRuntime_Apply(&reset_runtime, &first, 6U));
        reset_runtime.state = ABOX_MQTT_RUNTIME_BLOCKED;
        reset_runtime.blocked = 1U; /* uncertain communication cleanup */
        assert(ABoxMqttRuntime_NeedsModemRecovery(&reset_runtime));
        ABoxEc800At_Feed(&reset_at, (const uint8_t *)"RDY\r\n", 5U);
        assert(!reset_runtime.blocked && !reset_runtime.waiting);
        assert(reset_runtime.state == ABOX_MQTT_RUNTIME_PREPARED);
    }
    {
        Fixture delegated = {0};
        ABoxEc800At delegated_at;
        ABoxMqttRuntime delegated_runtime;
        at_port.context = &delegated;
        port.user = &delegated;
        port.mqtt_stop = transport_stop;
        assert(ABoxEc800At_Init(&delegated_at, &at_port));
        assert(ABoxMqttRuntime_Init(&delegated_runtime, &delegated_at, &port,
                                    &options, &first, 0U));
        delegated.connected = delegated.ready = 1U;
        ABoxMqttRuntime_Poll(&delegated_runtime, 1U);
        ABoxMqttRuntime_Poll(&delegated_runtime, 2U);
        ABoxMqttRuntime_Poll(&delegated_runtime, 3U);
        assert(ABoxMqttRuntime_IsReady(&delegated_runtime));
        assert(ABoxMqttRuntime_Stage(&delegated_runtime, &next));
        assert(ABoxMqttRuntime_Activate(&delegated_runtime, 4U));
        ABoxMqttRuntime_Poll(&delegated_runtime, 5U);
        assert(delegated.transport_stops == 1U && delegated.writes == 0U);
        assert(delegated_runtime.state == ABOX_MQTT_RUNTIME_DISCONNECT);
        delegated.transport_stop_result = 1;
        ABoxMqttRuntime_Poll(&delegated_runtime, 6U);
        assert(delegated_runtime.state == ABOX_MQTT_RUNTIME_PREPARED);
        delegated.connected = delegated.ready = 1U;
        ABoxMqttRuntime_Poll(&delegated_runtime, 7U);
        ABoxMqttRuntime_Poll(&delegated_runtime, 8U);
        ABoxMqttRuntime_Poll(&delegated_runtime, 9U);
        assert(ABoxMqttRuntime_IsReady(&delegated_runtime));
        assert(ABoxMqttRuntime_StopFirst(&delegated_runtime, 10U) == 0);
        ABoxMqttRuntime_Poll(&delegated_runtime, 11U);
        assert(delegated_runtime.state == ABOX_MQTT_RUNTIME_UNENROLLED);
        assert(delegated.writes == 0U);
    }
    {
        Fixture probed = {0};
        ABoxEc800At probe_at;
        ABoxMqttRuntime probe_runtime;
        const ABoxMqttConfig tls_probe_config = {"secure.example", 8883U, "u", "p", 1U, 0U};
        uint32_t now;
        options.probe_tls_capability = 1U;
        at_port.context = &probed;
        port.user = &probed;
        port.mqtt_stop = 0;
        assert(ABoxEc800At_Init(&probe_at, &at_port));
        assert(ABoxMqttRuntime_Init(&probe_runtime, &probe_at, &port,
                                    &options, &tls_probe_config, 0U));
        assert(probe_runtime.tls.supported_mask == 0U);
        for (now = 1U; now < 80U && !ABoxMqttRuntime_IsReady(&probe_runtime); ++now) {
            probed.now = now;
            if (probe_runtime.state == ABOX_MQTT_RUNTIME_CONNECT ||
                probe_runtime.state == ABOX_MQTT_RUNTIME_CONNECTED)
                probed.connected = probed.ready = 1U;
            ABoxMqttRuntime_Poll(&probe_runtime, now);
            ABoxEc800At_Task(&probe_at);
            if (probe_at.active_valid) {
                const char *reply = strstr(probed.last_command, "AT+QSSLCFG=?") ?
                    "+QSSLCFG: \"sslversion\",(0-5)\r\nOK\r\n" : "OK\r\n";
                ABoxEc800At_Feed(&probe_at, (const uint8_t *)reply,
                                 (uint16_t)strlen(reply));
            }
        }
        assert(ABoxMqttRuntime_IsReady(&probe_runtime));
        assert(probe_runtime.tls_probe_complete &&
               probe_runtime.tls.supported_mask == 4U);
        ABoxMqttRuntime_OnModemReset(&probe_runtime, now);
        assert(!probe_runtime.tls_probe_complete &&
               probe_runtime.tls.supported_mask == 0U);
    }
    {
        Fixture unsupported = {0};
        ABoxEc800At unsupported_at;
        ABoxMqttRuntime unsupported_runtime;
        const ABoxMqttConfig tls_probe_config = {"secure.example", 8883U, "u", "p", 1U, 0U};
        uint32_t now;
        at_port.context = &unsupported;
        port.user = &unsupported;
        assert(ABoxEc800At_Init(&unsupported_at, &at_port));
        assert(ABoxMqttRuntime_Init(&unsupported_runtime, &unsupported_at,
                                    &port, &options, &tls_probe_config, 0U));
        for (now = 1U; now < 20U && unsupported_runtime.state != ABOX_MQTT_RUNTIME_FAILED; ++now) {
            unsupported.now = now;
            ABoxMqttRuntime_Poll(&unsupported_runtime, now);
            ABoxEc800At_Task(&unsupported_at);
            if (unsupported_at.active_valid) {
                const char *reply = "+QSSLCFG: \"sslversion\",(0-1)\r\nOK\r\n";
                ABoxEc800At_Feed(&unsupported_at, (const uint8_t *)reply,
                                 (uint16_t)strlen(reply));
            }
        }
        assert(unsupported_runtime.state == ABOX_MQTT_RUNTIME_FAILED);
        assert(unsupported_runtime.safety_locked && !ABoxMqttRuntime_NeedsModemRecovery(&unsupported_runtime));
        assert(unsupported.paused && !unsupported.security);
        assert(!strstr(unsupported.last_command, "QMTOPEN"));
        ABoxEc800At_OnModemReset(&unsupported_at);
        assert(unsupported_runtime.state==ABOX_MQTT_RUNTIME_BLOCKED);
        assert(unsupported_runtime.safety_locked && unsupported.paused && !unsupported.security);
    }
    {
        Fixture comm = {0}; ABoxEc800At comm_at; ABoxMqttRuntime comm_runtime;
        const ABoxMqttConfig secure = {"secure.example", 8883U, "u", "p", 1U, 0U};
        options.probe_tls_capability=0;
        at_port.context=&comm; port.user=&comm;
        port.mqtt_stop=0; port.mqtt_modem_reset=0;
        assert(ABoxEc800At_Init(&comm_at,&at_port));
        assert(ABoxMqttRuntime_Init(&comm_runtime,&comm_at,&port,&options,&secure,0));
        ABoxMqttRuntime_Poll(&comm_runtime,1);
        ABoxEc800At_Task(&comm_at);
        ABoxEc800At_Feed(&comm_at,(const uint8_t *)"OK\r\n",4);
        ABoxMqttRuntime_Poll(&comm_runtime,2);
        ABoxMqttRuntime_Poll(&comm_runtime,3); ABoxEc800At_Task(&comm_at);
        ABoxEc800At_Feed(&comm_at,(const uint8_t *)"ERROR\r\n",7);
        ABoxMqttRuntime_Poll(&comm_runtime,4);
        assert(comm_runtime.state==ABOX_MQTT_RUNTIME_FAILED && comm_runtime.lease.generation);
        assert(ABoxMqttRuntime_StopFirst(&comm_runtime,4)==-1);
        assert(comm_runtime.blocked && !comm_runtime.safety_locked);
        assert(ABoxMqttRuntime_NeedsModemRecovery(&comm_runtime));
        ABoxEc800At_OnModemReset(&comm_at);
        assert(!comm_runtime.lease.generation && !comm_runtime.blocked);
        assert(comm_runtime.state==ABOX_MQTT_RUNTIME_WAIT_CA);
    }
    return 0;
}
