#include "abox_ec800_at.h"

#include <stdio.h>
#include <string.h>

#define ABORT_GUARD_MS 1500U

static uint32_t tick(const ABoxEc800At *at)
{
    return at->port.tick_ms ? at->port.tick_ms(at->port.context) : 0U;
}

static int write_bytes(ABoxEc800At *at, const uint8_t *data, uint16_t length,
                       uint32_t timeout_ms)
{
    return at->port.write &&
           at->port.write(at->port.context, data, length, timeout_ms);
}

static int prefix(const uint8_t *line, const char *value)
{
    return strncmp((const char *)line, value, strlen(value)) == 0;
}

static int payload_command(const char *command)
{
    if (!command) return 0;
    return strncmp(command, "AT+QMTPUBEX=", 12U) == 0 ||
           strncmp(command, "AT+QISEND=", 10U) == 0 ||
           strncmp(command, "AT+QHTTPURL=", 12U) == 0 ||
           strncmp(command, "AT+QHTTPPOST=", 13U) == 0 ||
           (strncmp(command, "AT+QFWRITE=", 11U) == 0 && command[11] != '?') ||
           strncmp(command, "AT+QFUPL=\"", 10U) == 0;
}

static int waits_for_urc(const char *command)
{
    if (!command) return 0;
    if (strncmp(command, "AT+QHTTPGET", 11U) == 0 && strchr(command, '=') &&
        strstr(command, "=?") == 0)
        return 1;
    if (strncmp(command, "AT+QHTTPPOST=", 13U) == 0)
        return 1;
    if (strncmp(command, "AT+QHTTPREAD=", 13U) == 0 && command[13] != '?')
        return 1;
    if (strncmp(command, "AT+QHTTPREADFILE=", 17U) == 0 && command[17] != '?')
        return 1;
    if (strncmp(command, "AT+QMTDISC=", 11U) == 0 ||
        strncmp(command, "AT+QMTCLOSE=", 12U) == 0)
        return 1;
    return 0;
}

static int terminal(const uint8_t *line, ABoxEc800Result *result)
{
    if (strcmp((const char *)line, "OK") == 0) *result = ABOX_EC800_RESULT_OK;
    else if (strcmp((const char *)line, "ERROR") == 0 ||
             prefix(line, "+CME ERROR"))
        *result = ABOX_EC800_RESULT_ERROR;
    else if (strcmp((const char *)line, "SEND OK") == 0)
        *result = ABOX_EC800_RESULT_SEND_OK;
    else if (strcmp((const char *)line, "SEND FAIL") == 0)
        *result = ABOX_EC800_RESULT_SEND_FAIL;
    else
        return 0;
    return 1;
}

static int result_urc(const ABoxEc800At *at, const uint8_t *line,
                      ABoxEc800Result *result)
{
    unsigned long a = 0U;
    unsigned long b = 0U;
    const char *command;
    if (!at->active_valid) return 0;
    command = at->active.command;
    if (strncmp(command, "AT+QHTTPGET", 11U) == 0 &&
        sscanf((const char *)line, "+QHTTPGET: %lu", &a) == 1) {
        *result = a == 0U ? ABOX_EC800_RESULT_OK : ABOX_EC800_RESULT_ERROR;
        return 1;
    }
    if (strncmp(command, "AT+QHTTPPOST=", 13U) == 0 &&
        sscanf((const char *)line, "+QHTTPPOST: %lu", &a) == 1) {
        *result = a == 0U ? ABOX_EC800_RESULT_OK : ABOX_EC800_RESULT_ERROR;
        return 1;
    }
    if (strncmp(command, "AT+QHTTPREADFILE=", 17U) == 0 &&
        sscanf((const char *)line, "+QHTTPREADFILE: %lu", &a) == 1) {
        *result = a == 0U ? ABOX_EC800_RESULT_OK : ABOX_EC800_RESULT_ERROR;
        return 1;
    }
    if (strncmp(command, "AT+QHTTPREAD=", 13U) == 0 &&
        sscanf((const char *)line, "+QHTTPREAD: %lu", &a) == 1) {
        *result = a == 0U ? ABOX_EC800_RESULT_OK : ABOX_EC800_RESULT_ERROR;
        return 1;
    }
    if (strncmp(command, "AT+QMTDISC=", 11U) == 0 &&
        sscanf((const char *)line, "+QMTDISC: %lu,%lu", &a, &b) == 2) {
        *result = b == 0U ? ABOX_EC800_RESULT_OK : ABOX_EC800_RESULT_ERROR;
        return 1;
    }
    if (strncmp(command, "AT+QMTCLOSE=", 12U) == 0 &&
        sscanf((const char *)line, "+QMTCLOSE: %lu,%lu", &a, &b) == 2) {
        *result = b == 0U ? ABOX_EC800_RESULT_OK : ABOX_EC800_RESULT_ERROR;
        return 1;
    }
    return 0;
}

static ABoxEc800Handler *handler(ABoxEc800At *at, ABoxEc800Owner owner)
{
    uint8_t i;
    for (i = 0U; i < ABOX_EC800_AT_HANDLER_SIZE; ++i)
        if (at->handlers[i].callback && at->handlers[i].owner == owner)
            return &at->handlers[i];
    return 0;
}

static void dispatch_owner(ABoxEc800At *at, ABoxEc800Owner owner,
                           ABoxEc800Event event, const uint8_t *data,
                           uint16_t length)
{
    ABoxEc800Handler *item = handler(at, owner);
    if (item) {
        ++at->notifying;
        item->callback(event, data, length, item->user);
        --at->notifying;
    }
}

static void dispatch_line(ABoxEc800At *at, const uint8_t *data, uint16_t length)
{
    ABoxEc800Owner target = ABOX_EC800_OWNER_NONE;
    uint8_t i;
    /* ATE may still be enabled after reset. Never dispatch credential echoes
     * or PSK readback to product observers/ordinary diagnostic logs. */
    if (prefix(data, "AT+QSSLCFG=\"psk\"") ||
        prefix(data, "+QSSLCFG: \"psk\"") || prefix(data, "AT+QMTCONN=")) return;
    if (length == 3U && memcmp(data, "RDY", 3U) == 0) {
        ABoxEc800At_OnModemReset(at);
        return;
    }
    if (at->port.route_line) {
        target = at->port.route_line(at->port.context, data, length,
                                     at->active_valid ? at->active.owner
                                                      : ABOX_EC800_OWNER_NONE);
    } else if (prefix(data, "+QMT")) {
        target = ABOX_EC800_OWNER_MQTT;
    } else if (at->active_valid) {
        target = at->active.owner;
    }
    if (target != ABOX_EC800_OWNER_NONE) {
        dispatch_owner(at, target, ABOX_EC800_EVENT_LINE, data, length);
        return;
    }
    ++at->notifying;
    for (i = 0U; i < ABOX_EC800_AT_HANDLER_SIZE; ++i)
        if (at->handlers[i].callback)
            at->handlers[i].callback(ABOX_EC800_EVENT_LINE, data, length,
                                     at->handlers[i].user);
    --at->notifying;
}

static void complete(ABoxEc800At *at, ABoxEc800Result result)
{
    ABoxEc800DoneFn done;
    void *user;
    ABoxEc800Owner owner;
    if (!at->active_valid) return;
    done = at->active.done;
    user = at->active.user;
    owner = at->active.owner;
    at->active_valid = 0U;
    at->active_payload_command = 0U;
    at->active_wait_payload = 0U;
    at->active_payload_sent = 0U;
    if (at->raw_owner == owner) {
        at->raw_owner = ABOX_EC800_OWNER_NONE;
        at->raw_remaining = 0U;
    }
    memset(&at->active, 0, sizeof(at->active));
    ++at->generation;
    if (done) {
        ++at->notifying;
        done(result, user);
        --at->notifying;
    }
}

static void emit_line(ABoxEc800At *at)
{
    ABoxEc800Result result;
    uint8_t *line;
    uint16_t length;
    int prompt;
    uint32_t generation = at->generation;
    if (at->line_length == 0U) return;
    at->line[at->line_length] = '\0';
    line = at->line;
    length = at->line_length;
    while (length && (*line == ' ' || *line == '\t')) {
        ++line;
        --length;
    }
    if (!length) {
        at->line_length = 0U;
        return;
    }
    prompt = (length == 1U && line[0] == '>') ||
             (length == 7U && memcmp(line, "CONNECT", 7U) == 0);
    if (at->active_valid && prompt && !at->active_payload_sent) {
        at->active_wait_payload = 1U;
        at->active_tick = tick(at);
    }
    dispatch_line(at, line, length);
    if (at->active_valid && generation == at->generation) {
        if (at->active_wait_payload && at->active.owner == ABOX_EC800_OWNER_MQTT &&
            prefix(line, "+QMTPUBEX:"))
            complete(at, ABOX_EC800_RESULT_OK);
        else if (result_urc(at, line, &result))
            complete(at, result);
        else if (terminal(line, &result)) {
            if (waits_for_urc(at->active.command) && result == ABOX_EC800_RESULT_OK) {
                /* The final URC owns completion. */
            } else if (!at->active_payload_command ||
                       result == ABOX_EC800_RESULT_ERROR ||
                       result == ABOX_EC800_RESULT_SEND_OK ||
                       result == ABOX_EC800_RESULT_SEND_FAIL ||
                       (result == ABOX_EC800_RESULT_OK && at->active_payload_sent))
                complete(at, result);
        }
    }
    at->line_length = 0U;
}

static void push(ABoxEc800At *at, uint8_t value)
{
    if (at->mqtt_rx_active) {
        at->mqtt_rx.feed(at->mqtt_rx.context, &value, 1U, tick(at));
        if (!at->mqtt_rx.collecting(at->mqtt_rx.context))
            at->mqtt_rx_active = 0U;
        return;
    }
    if (at->line_drop_until_lf) {
        if (value == '\n') at->line_drop_until_lf = 0U;
        return;
    }
    if (value == '\r') return;
    if (value == '>' && at->active_valid && at->active_payload_command &&
        !at->active_payload_sent) {
        if (at->line_length) emit_line(at);
        at->line[at->line_length++] = value;
        emit_line(at);
        return;
    }
    if (value == '\n') {
        emit_line(at);
        return;
    }
    if (!at->line_length && (value == ' ' || value == '\t')) return;
    if (at->line_length < ABOX_EC800_AT_LINE_SIZE - 1U)
        at->line[at->line_length++] = value;
    else {
        at->line_length = 0U;
        at->line_drop_until_lf = 1U;
        if (at->line_overflow_count != 0xFFFFFFFFUL)
            ++at->line_overflow_count;
    }
    if (at->mqtt_rx.feed && at->line_length == sizeof("+QMTRECV:") - 1U &&
        memcmp(at->line, "+QMTRECV:", sizeof("+QMTRECV:") - 1U) == 0) {
        at->mqtt_rx.feed(at->mqtt_rx.context, at->line, at->line_length, tick(at));
        at->line_length = 0U;
        at->mqtt_rx_active = (uint8_t)at->mqtt_rx.collecting(at->mqtt_rx.context);
    }
}

static int next_command(const ABoxEc800At *at)
{
    int best = -1;
    uint8_t i;
    for (i = 0U; i < ABOX_EC800_AT_QUEUE_SIZE; ++i) {
        if (!at->queue[i].used) continue;
        if (best < 0 || at->queue[i].priority > at->queue[best].priority)
            best = (int)i;
    }
    return best;
}

static void terminate_all(ABoxEc800At *at, ABoxEc800Result result);

static void start_next(ABoxEc800At *at)
{
    int index = next_command(at);
    uint16_t length;
    if (at->active_valid || index < 0) return;
    at->active = at->queue[index];
    memset(&at->queue[index], 0, sizeof(at->queue[index]));
    at->active_valid = 1U;
    ++at->generation;
    at->active_payload_command = (uint8_t)payload_command(at->active.command);
    at->active_wait_payload = 0U;
    at->active_payload_sent = 0U;
    at->active_tick = tick(at);
    length = (uint16_t)strlen(at->active.command);
    if (!write_bytes(at, (const uint8_t *)at->active.command, length, 300U)) {
        /* A failed write may have transmitted a prefix. Later commands must
         * not consume its untagged reply, including commands queued by done. */
        at->quarantined = 1U;
        terminate_all(at, ABOX_EC800_RESULT_SEND_FAIL);
    }
}

int ABoxEc800At_Init(ABoxEc800At *at, const ABoxEc800AtPort *port)
{
    if (!at || !port || !port->write || !port->tick_ms) return 0;
    memset(at, 0, sizeof(*at));
    at->port = *port;
    return 1;
}

int ABoxEc800At_SetMqttReceiver(ABoxEc800At *at,
                                 const ABoxEc800MqttRxPort *receiver)
{
    if (!at || at->active_valid || at->mqtt_rx_active ||
        (receiver && (!receiver->feed || !receiver->poll ||
                      !receiver->collecting || !receiver->locked))) return 0;
    if (receiver) at->mqtt_rx = *receiver;
    else memset(&at->mqtt_rx, 0, sizeof(at->mqtt_rx));
    return 1;
}

static void terminate_all(ABoxEc800At *at, ABoxEc800Result result)
{
    struct { ABoxEc800DoneFn done; void *user; } pending[ABOX_EC800_AT_QUEUE_SIZE + 1U];
    uint8_t count = 0U, i;
    if (!at || at->terminating) return;
    at->terminating = 1U;
    if (at->active_valid && at->active.done) {
        pending[count].done = at->active.done;
        pending[count++].user = at->active.user;
    }
    for (i = 0U; i < ABOX_EC800_AT_QUEUE_SIZE; ++i) {
        if (at->queue[i].used && at->queue[i].done) {
            pending[count].done = at->queue[i].done;
            pending[count++].user = at->queue[i].user;
        }
    }
    /* Detach every operation before invoking any callback, including callbacks
     * which cancel peers or request another reset. No command-sized snapshot. */
    ABoxEc800At_CancelAll(at);
    at->active_payload_command = at->active_wait_payload = at->active_payload_sent = 0U;
    at->line_length = 0U;
    ++at->generation;
    for (i = 0U; i < count; ++i) pending[i].done(result, pending[i].user);
    at->terminating = 0U;
}

void ABoxEc800At_Reset(ABoxEc800At *at)
{
    if (!at || at->terminating) return;
    terminate_all(at, ABOX_EC800_RESULT_ERROR);
    at->raw_owner = ABOX_EC800_OWNER_NONE;
    at->raw_remaining = at->active_tick = at->abort_guard_tick = 0U;
    at->line_length = 0U;
    at->line_drop_until_lf = at->quarantined = at->mqtt_rx_active = 0U;
}

void ABoxEc800At_OnModemReset(ABoxEc800At *at)
{
    static const uint8_t ready[] = "RDY";
    uint8_t i;
    if (!at || at->terminating) return;
    ABoxEc800At_Reset(at);
    at->terminating = 1U;
    for (i=0U; i<ABOX_EC800_AT_HANDLER_SIZE; ++i)
        if (at->handlers[i].callback)
            at->handlers[i].callback(ABOX_EC800_EVENT_LINE, ready, 3U, at->handlers[i].user);
    at->terminating = 0U;
}

void ABoxEc800At_Feed(ABoxEc800At *at, const uint8_t *data, uint16_t length)
{
    uint16_t index;
    if (!at || !data || at->terminating || at->driving || at->notifying) return;
    at->driving = 1U;
    for (index = 0U; index < length; ++index) {
        if (at->raw_remaining) {
            uint16_t take = (uint16_t)(length - index);
            if ((uint32_t)take > at->raw_remaining)
                take = (uint16_t)at->raw_remaining;
            ABoxEc800Owner owner = at->raw_owner;
            at->raw_remaining -= take;
            index = (uint16_t)(index + take - 1U);
            if (!at->raw_remaining) at->raw_owner = ABOX_EC800_OWNER_NONE;
            dispatch_owner(at, owner, ABOX_EC800_EVENT_RAW,
                           data + index + 1U - take, take);
        } else {
            push(at, data[index]);
        }
    }
    at->driving = 0U;
}

void ABoxEc800At_Task(ABoxEc800At *at)
{
    if (!at || at->quarantined || at->terminating || at->driving || at->notifying) return;
    at->driving = 1U;
    if (at->mqtt_rx_active) {
        at->mqtt_rx.poll(at->mqtt_rx.context, tick(at));
        if (at->mqtt_rx.locked(at->mqtt_rx.context)) {
            ABoxEc800At_Quarantine(at);
            at->driving = 0U;
            return;
        }
    }
    if (at->active_valid && tick(at) - at->active_tick >= at->active.timeout_ms) {
        if (at->active_payload_command && !at->active_payload_sent) {
            const uint8_t escape = 0x1BU;
            (void)write_bytes(at, &escape, 1U, 100U);
            at->abort_guard_tick = tick(at);
        }
        complete(at, ABOX_EC800_RESULT_TIMEOUT);
    }
    if (at->abort_guard_tick) {
        if (tick(at) - at->abort_guard_tick < ABORT_GUARD_MS) {
            at->driving = 0U;
            return;
        }
        at->abort_guard_tick = 0U;
    }
    if (!at->quarantined) start_next(at);
    at->driving = 0U;
}

void ABoxEc800At_Quarantine(ABoxEc800At *at)
{
    if (!at) return;
    at->quarantined = 1U;
    terminate_all(at, ABOX_EC800_RESULT_ERROR);
}

int ABoxEc800At_Submit(ABoxEc800At *at, const char *command,
                       ABoxEc800Owner owner, ABoxEc800Priority priority,
                       uint32_t timeout_ms, ABoxEc800DoneFn done, void *user)
{
    uint8_t i;
    uint16_t length;
    if (!at || at->quarantined || at->terminating || !command || !*command || owner == ABOX_EC800_OWNER_NONE) return 0;
    for (i = 0U; i < ABOX_EC800_AT_QUEUE_SIZE; ++i) {
        if (at->queue[i].used) continue;
        memset(&at->queue[i], 0, sizeof(at->queue[i]));
        length = (uint16_t)strlen(command);
        if (length > ABOX_EC800_AT_COMMAND_SIZE - 3U)
            length = ABOX_EC800_AT_COMMAND_SIZE - 3U;
        memcpy(at->queue[i].command, command, length);
        if (length < 2U || at->queue[i].command[length - 2U] != '\r' ||
            at->queue[i].command[length - 1U] != '\n') {
            at->queue[i].command[length++] = '\r';
            at->queue[i].command[length++] = '\n';
        }
        at->queue[i].command[length] = '\0';
        at->queue[i].owner = owner;
        at->queue[i].priority = priority;
        at->queue[i].timeout_ms = timeout_ms ? timeout_ms : 5000U;
        at->queue[i].done = done;
        at->queue[i].user = user;
        at->queue[i].used = 1U;
        return 1;
    }
    return 0;
}

int ABoxEc800At_SendPayload(ABoxEc800At *at, ABoxEc800Owner owner,
                            const uint8_t *data, uint16_t length)
{
    if (!at || !data || !length || !at->active_valid ||
        at->active.owner != owner || !at->active_payload_command ||
        !at->active_wait_payload || at->active_payload_sent)
        return 0;
    if (!write_bytes(at, data, length, 1000U)) {
        at->quarantined = 1U;
        terminate_all(at, ABOX_EC800_RESULT_SEND_FAIL);
        return 0;
    }
    at->active_wait_payload = 1U;
    at->active_payload_sent = 1U;
    at->active_tick = tick(at);
    return 1;
}

int ABoxEc800At_ForcePayload(ABoxEc800At *at, ABoxEc800Owner owner,
                             const uint8_t *data, uint16_t length)
{
    if (!at || !data || !length || !at->active_valid ||
        at->active.owner != owner || !at->active_payload_command ||
        at->active_payload_sent)
        return 0;
    at->active_wait_payload = 1U;
    return ABoxEc800At_SendPayload(at, owner, data, length);
}

int ABoxEc800At_BeginRaw(ABoxEc800At *at, ABoxEc800Owner owner,
                         uint32_t length)
{
    if (!at || at->terminating || at->quarantined || owner == ABOX_EC800_OWNER_NONE || !length ||
        length > ABOX_EC800_AT_RAW_MAX || at->raw_remaining)
        return 0;
    at->raw_owner = owner;
    at->raw_remaining = length;
    return 1;
}

int ABoxEc800At_Register(ABoxEc800At *at, ABoxEc800Owner owner,
                         ABoxEc800EventFn callback, void *user)
{
    uint8_t i;
    ABoxEc800Handler *item;
    if (!at || at->terminating || at->notifying || owner == ABOX_EC800_OWNER_NONE) return 0;
    item = handler(at, owner);
    if (item) {
        item->callback = callback;
        item->user = user;
        return 1;
    }
    if (!callback) return 1;
    for (i = 0U; i < ABOX_EC800_AT_HANDLER_SIZE; ++i) {
        if (!at->handlers[i].callback) {
            at->handlers[i].owner = owner;
            at->handlers[i].callback = callback;
            at->handlers[i].user = user;
            return 1;
        }
    }
    return 0;
}

int ABoxEc800At_HasPending(const ABoxEc800At *at, ABoxEc800Owner owner)
{
    uint8_t i;
    if (!at) return 0;
    if (at->active_valid && at->active.owner == owner) return 1;
    for (i = 0U; i < ABOX_EC800_AT_QUEUE_SIZE; ++i)
        if (at->queue[i].used && at->queue[i].owner == owner) return 1;
    return 0;
}

int ABoxEc800At_IsBusy(const ABoxEc800At *at)
{
    return at && (at->active_valid || next_command(at) >= 0);
}

int ABoxEc800At_IsActive(const ABoxEc800At *at, ABoxEc800Owner owner)
{
    return at && at->active_valid && at->active.owner == owner;
}

int ABoxEc800At_IsPayloadBusy(const ABoxEc800At *at)
{
    return at && at->active_valid && at->active_payload_command;
}

void ABoxEc800At_CancelQueued(ABoxEc800At *at, ABoxEc800Owner owner)
{
    uint8_t i;
    if (!at) return;
    for (i = 0U; i < ABOX_EC800_AT_QUEUE_SIZE; ++i)
        if (at->queue[i].used && at->queue[i].owner == owner)
            memset(&at->queue[i], 0, sizeof(at->queue[i]));
}

void ABoxEc800At_Cancel(ABoxEc800At *at, ABoxEc800Owner owner)
{
    if (!at || owner == ABOX_EC800_OWNER_NONE) return;
    if (at->active_valid && at->active.owner == owner) {
        if (at->active_payload_command && !at->active_payload_sent) {
            const uint8_t escape = 0x1BU;
            (void)write_bytes(at, &escape, 1U, 100U);
            at->abort_guard_tick = tick(at);
        }
        at->active_valid = 0U;
        ++at->generation;
        memset(&at->active, 0, sizeof(at->active));
    }
    if (at->raw_owner == owner) {
        at->raw_owner = ABOX_EC800_OWNER_NONE;
        at->raw_remaining = 0U;
    }
    ABoxEc800At_CancelQueued(at, owner);
}

void ABoxEc800At_CancelAll(ABoxEc800At *at)
{
    ABoxEc800Owner owner;
    if (!at) return;
    owner = at->active.owner;
    if (owner != ABOX_EC800_OWNER_NONE) ABoxEc800At_Cancel(at, owner);
    memset(at->queue, 0, sizeof(at->queue));
    at->raw_owner = ABOX_EC800_OWNER_NONE;
    at->raw_remaining = 0U;
}

void ABoxEc800At_AbortAll(ABoxEc800At *at, ABoxEc800Result result)
{
    if (!at) return;
    at->quarantined = 1U;
    terminate_all(at, result);
}

void ABoxEc800At_SetRxOverflowCount(ABoxEc800At *at, uint32_t count)
{
    if (at) at->rx_overflow_count = count;
}

uint32_t ABoxEc800At_RxOverflowCount(const ABoxEc800At *at)
{
    return at ? at->rx_overflow_count : 0U;
}

uint32_t ABoxEc800At_LineOverflowCount(const ABoxEc800At *at)
{
    return at ? at->line_overflow_count : 0U;
}
