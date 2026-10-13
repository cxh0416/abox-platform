#include "abox_mqtt_ec800.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { CMD_NONE, CMD_OK, CMD_FAILED };
enum { URC_NONE, URC_OK, URC_FAILED };
enum { URC_DRAIN_MS = 1500U };
enum { CFG_ECHO, CFG_RECV, CFG_VERSION, CFG_PDPCID, CFG_REG, CFG_ATTACH_QUERY, CFG_ATTACH,
       CFG_PDP_QUERY, CFG_APN, CFG_PDP_ACTIVATE, CFG_PDP_VERIFY, CFG_DONE };

static void change(ABoxMqttEc800 *m, ABoxMqttEc800State state, uint32_t now)
{
    m->state = state;
    m->entered_ms = now;
    m->command_pending = 0U;
    m->command_result = CMD_NONE;
    m->urc_pending = URC_NONE;
    m->urc_result = 0U;
    if (m->callbacks.state_changed)
        m->callbacks.state_changed(m->callbacks.user, state);
}

static void published(ABoxMqttEc800 *m, ABoxMqttEc800PublishEvent event)
{
    uint64_t operation = m->publish_operation;
    if (event != ABOX_MQTT_EC800_PUBLISH_SUBMITTED) {
        m->publish_active = 0U;
        m->publish_prompt = 0U;
        m->publish_operation = 0U;
    }
    if (operation && m->callbacks.publish_event)
        m->callbacks.publish_event(m->callbacks.user, operation, event);
}

static void done(ABoxEc800Result result, void *user)
{
    ABoxMqttEc800 *m = (ABoxMqttEc800 *)user;
    if (!m || !m->initialized) return;
    m->entered_ms = m->at->active_tick;
    m->command_pending = 0U;
    m->command_result = result == ABOX_EC800_RESULT_OK ||
                        result == ABOX_EC800_RESULT_SEND_OK ? CMD_OK : CMD_FAILED;
    if (m->command_result == CMD_FAILED) {
        ++m->counters.command_failed;
        if (m->publish_active) published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
        if (result == ABOX_EC800_RESULT_TIMEOUT)
            ABoxEc800At_Quarantine(m->at);
        if (m->state == ABOX_MQTT_EC800_READY)
            m->close_requested = 1U;
    }
}

static int submit(ABoxMqttEc800 *m, const char *command, uint32_t timeout)
{
    if (m->command_pending || m->at->quarantined ||
        !ABoxEc800At_Submit(m->at, command, ABOX_EC800_OWNER_MQTT,
                            ABOX_EC800_PRIORITY_HIGH, timeout, done, m))
        return 0;
    m->command_pending = 1U;
    m->command_result = CMD_NONE;
    m->entered_ms = m->at->port.tick_ms(m->at->port.context);
    return 1;
}

static int command_expired(ABoxMqttEc800 *m, uint32_t now, uint32_t timeout)
{
    if (m->command_pending) {
        /* The AT owner supplies the actual wire start, not queue admission.
         * Queued commands terminate through the AT completion contract. */
        if (!ABoxEc800At_IsActive(m->at, ABOX_EC800_OWNER_MQTT)) return 0;
        m->entered_ms = m->at->active_tick;
    }
    return (uint32_t)(now - m->entered_ms) >= timeout;
}

static void message(void *user, const uint8_t *topic, size_t topic_length,
                    const uint8_t *payload, size_t payload_length)
{
    ABoxMqttEc800 *m = (ABoxMqttEc800 *)user;
    if (m->state == ABOX_MQTT_EC800_READY && m->security_ready && !m->paused &&
        !m->workspace_borrowed && m->callbacks.message)
        m->callbacks.message(m->callbacks.user, topic, topic_length,
                             payload, payload_length);
}

static void rx_feed(void *context, const uint8_t *data, uint16_t length,
                    uint32_t now)
{
    ABoxMqttEc800 *m = (ABoxMqttEc800 *)context;
    ABoxMqttEc800Rx_Feed(&m->rx, data, length, now);
}
static void rx_poll(void *context, uint32_t now)
{
    ABoxMqttEc800 *m = (ABoxMqttEc800 *)context;
    ABoxMqttEc800Rx_Poll(&m->rx, now);
}
static int rx_collecting(void *context)
{
    ABoxMqttEc800 *m = (ABoxMqttEc800 *)context;
    return ABoxMqttEc800Rx_IsCollecting(&m->rx);
}
static int rx_locked(void *context)
{
    ABoxMqttEc800 *m = (ABoxMqttEc800 *)context;
    return ABoxMqttEc800Rx_IsLocked(&m->rx);
}

static int numbers(const char *line, const char *prefix,
                   unsigned *a, unsigned *b, int *c)
{
    size_t n = strlen(prefix);
    if (strncmp(line, prefix, n)) return 0;
    return sscanf(line + n, " %u,%u,%d", a, b, c);
}

static void event(ABoxEc800Event kind, const uint8_t *bytes,
                  uint16_t length, void *user)
{
    ABoxMqttEc800 *m = (ABoxMqttEc800 *)user;
    char line[80];
    unsigned client = 0U, id = 0U;
    int result = -1, count;
    if (!m || kind != ABOX_EC800_EVENT_LINE || !bytes) return;
    if (length >= sizeof(line)) { ++m->counters.urc_rejected; return; }
    memcpy(line, bytes, length);
    line[length] = '\0';
    if (!strcmp(line, "RDY")) {
        uint32_t now = m->at->port.tick_ms(m->at->port.context);
        if (m->callbacks.modem_reset)
            m->callbacks.modem_reset(m->callbacks.user);
        ABoxMqttEc800_OnModemReset(m, now);
        return;
    }
    if (!strncmp(line, "+CEREG:", 7U)) {
        unsigned status = 0U;
        if (sscanf(line + 7U, " %u,%u", &id, &status) == 2)
            m->registered = status == 1U || status == 5U;
        return;
    }
    if (!strncmp(line, "+CGATT:", 7U)) {
        if (sscanf(line + 7U, " %u", &id) == 1)
            m->attached = id == 1U;
        return;
    }
    if (!strncmp(line, "+QIACT:", 7U)) {
        unsigned state = 0U;
        if (sscanf(line + 7U, " %u,%u", &id, &state) == 2 && id == 1U)
            m->pdp_active = state == 1U;
        return;
    }
    if (length == 1U && line[0] == '>' && m->publish_active &&
        !m->publish_prompt && m->state == ABOX_MQTT_EC800_READY) {
        m->publish_prompt = 1U;
        if (ABoxEc800At_SendPayload(m->at, ABOX_EC800_OWNER_MQTT,
                                    m->buffers.publish_payload,
                                    m->publish_length))
            published(m, ABOX_MQTT_EC800_PUBLISH_SUBMITTED);
        else {
            published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
            m->close_requested = 1U;
        }
        return;
    }
    count = numbers(line, "+QMTPUBEX:", &client, &id, &result);
    if (count == 3) {
        if (!m->publish_active || !m->publish_prompt ||
            client != m->config.client_index ||
            id != m->publish_msg_id || m->state != ABOX_MQTT_EC800_READY) {
            ++m->counters.stale_publish;
            return;
        }
        published(m, result == 0 ? ABOX_MQTT_EC800_PUBLISH_CONFIRMED
                                  : ABOX_MQTT_EC800_PUBLISH_FAILED);
        if (result != 0) m->close_requested = 1U;
        return;
    }
    count = numbers(line, "+QMTOPEN:", &client, &id, &result);
    if (count == 2 && m->state == ABOX_MQTT_EC800_OPENING &&
        client == m->config.client_index) {
        m->urc_pending = id == 0U ? URC_OK : URC_FAILED;
        /* -1 is a failed open, not an established MQTT session. */
        m->urc_result = id == UINT_MAX ? 1U : 0U;
    }
    else if (count > 0 && !strncmp(line, "+QMTOPEN:", 9U))
        ++m->counters.urc_rejected;
    count = numbers(line, "+QMTCONN:", &client, &id, &result);
    if (count == 3 && m->state == ABOX_MQTT_EC800_CONNECTING &&
        client == m->config.client_index) {
        m->urc_pending = id == 0U && result == 0 ? URC_OK : URC_FAILED;
        /* A received negative CONNACK closes the server-side MQTT transport. */
        m->urc_result = id == 0U && result >= 1 && result <= 5 ? 2U : 0U;
    }
    else if (count > 0 && !strncmp(line, "+QMTCONN:", 9U))
        ++m->counters.urc_rejected;
    count = numbers(line, "+QMTSUB:", &client, &id, &result);
    if (count == 3 && m->state == ABOX_MQTT_EC800_SUBSCRIBING &&
        client == m->config.client_index && id == m->pending_sub_msg_id)
        m->urc_pending = result == 0 || result == 1 ? URC_OK : URC_FAILED;
    else if (count > 0 && !strncmp(line, "+QMTSUB:", 8U))
        ++m->counters.urc_rejected;
    if (!strncmp(line, "+QMTSTAT:", 9U) &&
        sscanf(line + 9U, " %u", &client) == 1 &&
        client == m->config.client_index &&
        m->state == ABOX_MQTT_EC800_READY) {
        ++m->counters.disconnects;
        m->close_requested = 1U;
    }
    if (m->state == ABOX_MQTT_EC800_RETRY_WAIT &&
        !strncmp(line, "+QMT", 4U))
        m->entered_ms = m->at->port.tick_ms(m->at->port.context);
}

static void fail_closed(ABoxMqttEc800 *m, uint32_t now)
{
    uint8_t refused = m->state == ABOX_MQTT_EC800_CONNECTING &&
                      m->urc_pending == URC_FAILED && m->urc_result == 2U;
    if (m->publish_active) published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
    if (m->command_pending || m->at->quarantined) {
        ABoxEc800At_Quarantine(m->at);
        change(m, ABOX_MQTT_EC800_BLOCKED, now);
        return;
    }
    if (m->state == ABOX_MQTT_EC800_CONFIGURING) {
        /* No MQTT session exists yet. Registration and PDP setup can be
         * transient after modem power-up; QMTDISC/QMTCLOSE are invalid here. */
        change(m, ABOX_MQTT_EC800_RETRY_WAIT, now);
        return;
    }
    /* No MQTT session exists after explicit refusal. Still close any retained
     * modem client; ERROR here means the rejected client is already gone. */
    m->close_step = refused ? 2U : 0U;
    change(m, ABOX_MQTT_EC800_CLOSING, now);
}

static int valid_text(const char *s, size_t limit)
{
    size_t i;
    if (!s || !*s) return 0;
    for (i = 0U; i < limit && s[i]; ++i)
        if (s[i] == '"' || s[i] == '\r' || s[i] == '\n') return 0;
    return i < limit;
}

static int valid_config(const ABoxMqttEc800Config *c, uint8_t endpoint)
{
    uint8_t i;
    if (!c || !c->command_timeout_ms || !c->open_timeout_ms ||
        !c->connect_timeout_ms || !c->subscribe_timeout_ms ||
        !c->publish_timeout_ms || !c->retry_delay_ms ||
        c->subscription_count > 8U || c->legacy_json > 1U ||
        c->mqtt_version > 4U || c->pdp_context_id > 16U ||
        (c->subscription_count && !c->subscriptions) ||
        (c->apn && !valid_text(c->apn, 64U)) ||
        (c->username && *c->username && !valid_text(c->username, 32U)) ||
        (c->password && *c->password && !valid_text(c->password, 64U)))
        return 0;
    if (endpoint && (!valid_text(c->host, 64U) ||
                     !valid_text(c->client_id, 64U) || !c->port))
        return 0;
    for (i = 0U; i < c->subscription_count; ++i)
        if (!valid_text(c->subscriptions[i], 128U)) return 0;
    return 1;
}

int ABoxMqttEc800_Init(ABoxMqttEc800 *m, ABoxEc800At *at,
                        const ABoxMqttEc800Config *c,
                        const ABoxMqttEc800Buffers *b,
                        const ABoxMqttEc800Callbacks *callbacks, uint32_t now)
{
    ABoxEc800MqttRxPort route;
    if (!m || !at || !c || !b || !callbacks || !callbacks->message ||
        !valid_config(c, 0U) ||
        !b->publish_payload || !b->publish_capacity) return 0;
    memset(m, 0, sizeof(*m));
    m->at = at; m->config = *c; m->buffers = *b; m->callbacks = *callbacks;
    if (!ABoxMqttEc800Rx_Init(&m->rx, b->header, b->header_capacity,
                              b->topic, b->topic_capacity, b->payload,
                              b->payload_capacity, c->command_timeout_ms,
                              c->legacy_json, message, m)) return 0;
    memset(&route, 0, sizeof(route));
    route.context = m; route.feed = rx_feed; route.poll = rx_poll;
    route.collecting = rx_collecting; route.locked = rx_locked;
    if (!ABoxEc800At_SetMqttReceiver(at, &route)) return 0;
    if (!ABoxEc800At_Register(at, ABOX_EC800_OWNER_MQTT, event, m)) {
        (void)ABoxEc800At_SetMqttReceiver(at, 0);
        return 0;
    }
    m->initialized = 1U;
    change(m, ABOX_MQTT_EC800_IDLE, now);
    return 1;
}

int ABoxMqttEc800_Configure(ABoxMqttEc800 *m,
                             const ABoxMqttEc800Config *config)
{
    if (!m || !m->initialized || !valid_config(config, 1U) ||
        m->workspace_borrowed ||
        (m->state != ABOX_MQTT_EC800_IDLE &&
         m->state != ABOX_MQTT_EC800_PAUSED) ||
        ABoxEc800At_HasPending(m->at, ABOX_EC800_OWNER_MQTT))
        return 0;
    m->config = *config;
    m->rx.legacy_json = config->legacy_json;
    ABoxMqttEc800Rx_Reset(&m->rx);
    return 1;
}

int ABoxMqttEc800_Start(ABoxMqttEc800 *m, uint32_t now)
{
    if (!m || !m->initialized || !valid_config(&m->config, 1U) ||
        !m->security_ready ||
        m->paused || m->workspace_borrowed ||
        m->at->quarantined || m->state == ABOX_MQTT_EC800_BLOCKED) return 0;
    m->enabled = 1U;
    m->step = CFG_ECHO;
    ++m->generation;
    change(m, ABOX_MQTT_EC800_CONFIGURING, now);
    return 1;
}

static void configure(ABoxMqttEc800 *m, uint32_t now)
{
    char command[128];
    if (!m->command_pending && m->command_result == CMD_NONE) {
        if (command_expired(m, now, m->config.command_timeout_ms)) {
            fail_closed(m, now); return;
        }
        switch (m->step) {
        case CFG_ECHO: strcpy(command, "ATE0"); break;
        case CFG_RECV:
            snprintf(command, sizeof(command), "AT+QMTCFG=\"recv/mode\",%u,0,0",
                     m->config.client_index); break;
        case CFG_VERSION:
            if (!m->config.mqtt_version) { ++m->step; m->entered_ms = now; return; }
            snprintf(command, sizeof(command), "AT+QMTCFG=\"version\",%u,%u",
                     m->config.client_index, m->config.mqtt_version); break;
        case CFG_PDPCID:
            if (!m->config.pdp_context_id) { ++m->step; m->entered_ms = now; return; }
            snprintf(command, sizeof(command), "AT+QMTCFG=\"pdpcid\",%u,%u",
                     m->config.client_index, m->config.pdp_context_id); break;
        case CFG_REG: strcpy(command, "AT+CEREG?"); m->registered = 0U; break;
        case CFG_ATTACH_QUERY: strcpy(command, "AT+CGATT?"); m->attached = 0U; break;
        case CFG_ATTACH: strcpy(command, "AT+CGATT=1"); break;
        case CFG_PDP_QUERY: strcpy(command, "AT+QIACT?"); m->pdp_active = 0U; break;
        case CFG_APN:
            snprintf(command, sizeof(command), "AT+QICSGP=1,1,\"%s\",\"\",\"\",1",
                     m->config.apn ? m->config.apn : "CMNET"); break;
        case CFG_PDP_ACTIVATE: strcpy(command, "AT+QIACT=1"); break;
        case CFG_PDP_VERIFY: strcpy(command, "AT+QIACT?"); m->pdp_active = 0U; break;
        default: change(m, ABOX_MQTT_EC800_OPENING, now); return;
        }
        /* Rejected admission keeps this step and its bounded queue deadline. */
        if (!submit(m, command, m->config.command_timeout_ms)) return;
        return;
    }
    if (m->command_result == CMD_FAILED) { fail_closed(m, now); return; }
    if (m->command_result != CMD_OK) return;
    m->command_result = CMD_NONE;
    m->entered_ms = now;
    switch (m->step) {
    case CFG_REG:
        if (!m->registered) { fail_closed(m, now); return; }
        break;
    case CFG_ATTACH_QUERY:
        if (!m->attached) { m->step = CFG_ATTACH; return; }
        m->step = CFG_PDP_QUERY; return;
    case CFG_ATTACH: m->step = CFG_PDP_QUERY; return;
    case CFG_PDP_QUERY:
        if (m->pdp_active) { change(m, ABOX_MQTT_EC800_OPENING, now); return; }
        m->step = CFG_APN; return;
    case CFG_PDP_VERIFY:
        if (!m->pdp_active) { fail_closed(m, now); return; }
        change(m, ABOX_MQTT_EC800_OPENING, now); return;
    default: break;
    }
    ++m->step;
}

static void opening(ABoxMqttEc800 *m, uint32_t now)
{
    char command[128];
    if (!m->command_pending && m->command_result == CMD_NONE) {
        if (command_expired(m, now, m->config.open_timeout_ms)) {
            fail_closed(m, now); return;
        }
        snprintf(command, sizeof(command), "AT+QMTOPEN=%u,\"%s\",%u",
                 m->config.client_index, m->config.host, m->config.port);
        if (!submit(m, command, m->config.open_timeout_ms)) return;
    }
    if (m->urc_pending == URC_FAILED && m->urc_result == 1U &&
        m->command_result == CMD_OK && !m->command_pending) {
        /* QMTOPEN:-1 proves no socket was opened. Disconnect/close on this
         * client return ERROR and must not quarantine an in-sync AT port. */
        change(m, ABOX_MQTT_EC800_RETRY_WAIT, now);
        return;
    }
    if (m->urc_pending == URC_FAILED && m->command_pending) return;
    if (m->command_result == CMD_FAILED || m->urc_pending == URC_FAILED ||
        command_expired(m, now, m->config.open_timeout_ms)) {
        fail_closed(m, now); return;
    }
    if (m->urc_pending == URC_OK && m->command_result == CMD_OK)
        change(m, ABOX_MQTT_EC800_CONNECTING, now);
}

static void connecting(ABoxMqttEc800 *m, uint32_t now)
{
    char command[240];
    if (!m->command_pending && m->command_result == CMD_NONE) {
        if (command_expired(m, now, m->config.connect_timeout_ms)) {
            fail_closed(m, now); return;
        }
        if (m->config.username && *m->config.username)
            snprintf(command, sizeof(command), "AT+QMTCONN=%u,\"%s\",\"%s\",\"%s\"",
                     m->config.client_index, m->config.client_id,
                     m->config.username, m->config.password ? m->config.password : "");
        else
            snprintf(command, sizeof(command), "AT+QMTCONN=%u,\"%s\"",
                     m->config.client_index, m->config.client_id);
        if (!submit(m, command, m->config.connect_timeout_ms)) return;
    }
    if (m->command_result == CMD_FAILED || m->urc_pending == URC_FAILED ||
        command_expired(m, now, m->config.connect_timeout_ms)) {
        fail_closed(m, now); return;
    }
    if (m->urc_pending == URC_OK && m->command_result == CMD_OK) {
        m->subscription_index = 0U;
        change(m, ABOX_MQTT_EC800_SUBSCRIBING, now);
    }
}

static void subscribing(ABoxMqttEc800 *m, uint32_t now)
{
    char command[180];
    if (m->subscription_index == m->config.subscription_count) {
        change(m, ABOX_MQTT_EC800_READY, now); return;
    }
    if (!m->command_pending && m->command_result == CMD_NONE) {
        uint16_t message_id = (uint16_t)(m->next_sub_msg_id + 1U);
        if (command_expired(m, now, m->config.subscribe_timeout_ms)) {
            fail_closed(m, now); return;
        }
        if (!message_id) ++message_id;
        snprintf(command, sizeof(command), "AT+QMTSUB=%u,%u,\"%s\",1",
                 m->config.client_index, message_id,
                 m->config.subscriptions[m->subscription_index]);
        if (!submit(m, command, m->config.subscribe_timeout_ms)) return;
        m->pending_sub_msg_id = m->next_sub_msg_id = message_id;
    }
    if (m->command_result == CMD_FAILED || m->urc_pending == URC_FAILED ||
        command_expired(m, now, m->config.subscribe_timeout_ms)) {
        fail_closed(m, now); return;
    }
    if (m->urc_pending == URC_OK && m->command_result == CMD_OK) {
        ++m->subscription_index;
        m->command_result = CMD_NONE;
        m->urc_pending = URC_NONE;
        m->entered_ms = now;
    }
}

static void closing(ABoxMqttEc800 *m, uint32_t now)
{
    char command[32];
    if (m->at->quarantined) { change(m, ABOX_MQTT_EC800_BLOCKED, now); return; }
    if (!m->command_pending && m->command_result == CMD_NONE) {
        snprintf(command, sizeof(command), m->close_step ? "AT+QMTCLOSE=%u" :
                 "AT+QMTDISC=%u", m->config.client_index);
        if (command_expired(m, now, m->config.command_timeout_ms)) {
            ABoxEc800At_Quarantine(m->at);
            change(m, ABOX_MQTT_EC800_BLOCKED, now);
        } else if (!submit(m, command, m->config.command_timeout_ms)) return;
        return;
    }
    if (m->command_result == CMD_FAILED && m->close_step && m->close_step != 2U) {
        ABoxEc800At_Quarantine(m->at);
        change(m, ABOX_MQTT_EC800_BLOCKED, now);
        return;
    }
    if (m->command_result == CMD_NONE) return;
    m->command_result = CMD_NONE;
    if (!m->close_step++) {
        m->entered_ms = now;
        return;
    }
    /* No new operation or workspace loan until untagged URCs stop arriving. */
    if (m->enabled) ++m->counters.reconnects;
    change(m, ABOX_MQTT_EC800_RETRY_WAIT, now);
}

void ABoxMqttEc800_Poll(ABoxMqttEc800 *m, uint32_t now)
{
    if (!m || !m->initialized) return;
    ABoxMqttEc800Rx_Poll(&m->rx, now);
    if (m->at->quarantined || ABoxMqttEc800Rx_IsLocked(&m->rx)) {
        /* The publish ACK wait can outlive its AT command (SEND OK). Settle
         * it even if safety policy prevents immediate physical recovery. */
        if (m->publish_active) published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
        if (m->state != ABOX_MQTT_EC800_BLOCKED)
            change(m, ABOX_MQTT_EC800_BLOCKED, now);
        return;
    }
    /* A stop may arrive between QMTOPEN's OK and its delayed result URC.
     * Resolve that in-flight open before deciding whether a client exists. */
    if (m->close_requested && m->state == ABOX_MQTT_EC800_OPENING) {
        opening(m, now);
        if (m->state == ABOX_MQTT_EC800_OPENING) return;
        if (m->state == ABOX_MQTT_EC800_RETRY_WAIT) m->close_requested = 0U;
    }
    if (m->close_requested && m->state != ABOX_MQTT_EC800_CLOSING &&
        m->state != ABOX_MQTT_EC800_RETRY_WAIT) {
        if (m->command_pending) return;
        m->close_requested = 0U;
        fail_closed(m, now);
        return;
    }
    if (m->state == ABOX_MQTT_EC800_READY) {
        if (m->publish_active &&
                   (uint32_t)(now - m->last_publish_ms) >=
                   m->config.publish_timeout_ms) {
            ++m->counters.publish_timeout;
            published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
            fail_closed(m, now);
        }
        return;
    }
    switch (m->state) {
    case ABOX_MQTT_EC800_CONFIGURING: configure(m, now); break;
    case ABOX_MQTT_EC800_OPENING: opening(m, now); break;
    case ABOX_MQTT_EC800_CONNECTING: connecting(m, now); break;
    case ABOX_MQTT_EC800_SUBSCRIBING: subscribing(m, now); break;
    case ABOX_MQTT_EC800_CLOSING: closing(m, now); break;
    case ABOX_MQTT_EC800_RETRY_WAIT:
        if ((uint32_t)(now - m->entered_ms) < URC_DRAIN_MS ||
            ABoxEc800At_IsBusy(m->at)) break;
        if (m->paused) {
            change(m, ABOX_MQTT_EC800_PAUSED, now);
        } else if (m->enabled && m->security_ready &&
            (uint32_t)(now - m->entered_ms) >= m->config.retry_delay_ms) {
            ++m->generation;
            m->step = CFG_ECHO;
            change(m, ABOX_MQTT_EC800_CONFIGURING, now);
        } else if (!m->enabled) change(m, ABOX_MQTT_EC800_IDLE, now);
        break;
    default: break;
    }
}

void ABoxMqttEc800_SetSecurityReady(ABoxMqttEc800 *m, uint8_t ready)
{
    if (!m) return;
    m->security_ready = ready ? 1U : 0U;
    /* Runtime may stop polling the transport after a safety latch. Resolve
     * its publish receipt here, including an ACK wait with no active AT. */
    if (!m->security_ready && m->publish_active)
        published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
}

void ABoxMqttEc800_SetPausedSoft(ABoxMqttEc800 *m, uint8_t paused)
{
    if (m) m->paused = paused ? 1U : 0U;
}

void ABoxMqttEc800_RequestReconnect(ABoxMqttEc800 *m, uint32_t now)
{
    if (!m || !m->initialized || m->state == ABOX_MQTT_EC800_BLOCKED) return;
    m->enabled = 1U;
    ++m->generation;
    if (m->command_pending) m->close_requested = 1U;
    else fail_closed(m, now);
}

int ABoxMqttEc800_Stop(ABoxMqttEc800 *m, uint32_t now)
{
    if (!m || !m->initialized) return -1;
    if (m->state == ABOX_MQTT_EC800_BLOCKED) return -1;
    m->enabled = 0U;
    if (m->state == ABOX_MQTT_EC800_IDLE ||
        m->state == ABOX_MQTT_EC800_PAUSED) return 1;
    if (m->state == ABOX_MQTT_EC800_RETRY_WAIT) return 0;
    if (m->state == ABOX_MQTT_EC800_OPENING) {
        if (!m->command_pending && m->command_result == CMD_NONE)
            change(m, ABOX_MQTT_EC800_RETRY_WAIT, now);
        else m->close_requested = 1U;
        return 0;
    }
    if (m->state != ABOX_MQTT_EC800_CLOSING) {
        if (m->command_pending) m->close_requested = 1U;
        else fail_closed(m, now);
    }
    return m->state == ABOX_MQTT_EC800_BLOCKED ? -1 : 0;
}

int ABoxMqttEc800_Pause(ABoxMqttEc800 *m, uint32_t now)
{
    int result;
    if (!m) return -1;
    m->paused = 1U;
    result = ABoxMqttEc800_Stop(m, now);
    if (result == 1 && m->state != ABOX_MQTT_EC800_PAUSED)
        change(m, ABOX_MQTT_EC800_PAUSED, now);
    return result;
}

int ABoxMqttEc800_Resume(ABoxMqttEc800 *m, uint32_t now)
{
    if (!m || m->workspace_borrowed ||
        m->state != ABOX_MQTT_EC800_PAUSED) return 0;
    ABoxMqttEc800Rx_Reset(&m->rx);
    m->paused = 0U;
    return ABoxMqttEc800_Start(m, now);
}

void ABoxMqttEc800_OnModemReset(ABoxMqttEc800 *m, uint32_t now)
{
    if (!m || !m->initialized) return;
    if (m->publish_active) published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
    ++m->generation;
    ABoxMqttEc800Rx_Reset(&m->rx);
    m->command_pending = 0U;
    m->close_requested = 0U;
    m->step = CFG_ECHO;
    if (m->enabled && !m->paused && m->security_ready &&
        !m->workspace_borrowed && !m->at->quarantined)
        change(m, ABOX_MQTT_EC800_CONFIGURING, now);
    else change(m, m->paused ? ABOX_MQTT_EC800_PAUSED :
                ABOX_MQTT_EC800_IDLE, now);
}

int ABoxMqttEc800_Publish(ABoxMqttEc800 *m, const char *topic,
                           const uint8_t *payload, size_t length,
                           uint8_t qos, uint8_t retain, uint64_t *operation)
{
    char command[200];
    uint16_t msg_id;
    if (!m || !topic || !payload || !operation || !length ||
        length > m->buffers.publish_capacity || length > UINT16_MAX ||
        !valid_text(topic, 128U) || qos > 1U || retain > 1U ||
        m->state != ABOX_MQTT_EC800_READY || !m->security_ready ||
        m->publish_active || m->workspace_borrowed ||
        ABoxEc800At_IsBusy(m->at)) return 0;
    /* Quectel requires msgid 0 for QoS 0. QoS 1 uses a nonzero ID for
     * the modem's publish completion URC. */
    if (qos) {
        ++m->next_msg_id;
        if (!m->next_msg_id) ++m->next_msg_id;
    }
    msg_id = qos ? m->next_msg_id : 0U;
    snprintf(command, sizeof(command), "AT+QMTPUBEX=%u,%u,%u,%u,\"%s\",%u",
             m->config.client_index, msg_id, qos, retain,
             topic, (unsigned)length);
    memcpy(m->buffers.publish_payload, payload, length);
    if (!submit(m, command, m->config.publish_timeout_ms)) return 0;
    ++m->next_operation;
    if (!m->next_operation) ++m->next_operation;
    m->publish_operation = m->next_operation;
    m->publish_msg_id = msg_id;
    m->publish_length = (uint16_t)length;
    m->publish_active = 1U;
    m->publish_prompt = 0U;
    m->last_publish_ms = m->at->port.tick_ms(m->at->port.context);
    *operation = m->publish_operation;
    return 1;
}

void ABoxMqttEc800_CancelPublish(ABoxMqttEc800 *m)
{
    if (!m || !m->publish_active) return;
    published(m, ABOX_MQTT_EC800_PUBLISH_FAILED);
    if (ABoxEc800At_IsActive(m->at, ABOX_EC800_OWNER_MQTT)) {
        ABoxEc800At_Quarantine(m->at);
        change(m, ABOX_MQTT_EC800_BLOCKED,
               m->at->port.tick_ms(m->at->port.context));
    } else ABoxEc800At_CancelQueued(m->at, ABOX_EC800_OWNER_MQTT);
}

uint8_t *ABoxMqttEc800_BorrowWorkspace(ABoxMqttEc800 *m, size_t *capacity)
{
    if (!m || m->state != ABOX_MQTT_EC800_PAUSED ||
        m->workspace_borrowed || ABoxEc800At_IsBusy(m->at) ||
        ABoxMqttEc800Rx_IsCollecting(&m->rx)) return 0;
    m->workspace_borrowed = 1U;
    if (capacity) *capacity = m->rx.payload_capacity;
    return m->rx.payload;
}

int ABoxMqttEc800_ReturnWorkspace(ABoxMqttEc800 *m)
{
    if (!m || !m->workspace_borrowed) return 0;
    m->workspace_borrowed = 0U;
    ABoxMqttEc800Rx_Reset(&m->rx);
    return 1;
}

ABoxMqttEc800State ABoxMqttEc800_GetState(const ABoxMqttEc800 *m)
{
    return m ? m->state : ABOX_MQTT_EC800_BLOCKED;
}
int ABoxMqttEc800_IsReady(const ABoxMqttEc800 *m)
{
    return m && m->state == ABOX_MQTT_EC800_READY &&
           m->security_ready && !m->paused;
}
int ABoxMqttEc800_IsConnected(const ABoxMqttEc800 *m)
{
    return m && (m->state == ABOX_MQTT_EC800_SUBSCRIBING ||
                 m->state == ABOX_MQTT_EC800_READY);
}
void ABoxMqttEc800_GetCounters(const ABoxMqttEc800 *m,
                                ABoxMqttEc800Counters *out)
{
    if (!m || !out) return;
    memset(out, 0, sizeof(*out));
    *out = m->counters;
    out->receive = m->rx.counters;
}
