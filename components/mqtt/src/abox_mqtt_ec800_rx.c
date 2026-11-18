#include "abox_mqtt_ec800_rx.h"

#include <limits.h>
#include <string.h>

enum {
    RX_SEARCH, RX_HEADER, RX_AFTER_TOPIC, RX_LENGTH, RX_LENGTH_START, RX_RAW,
    RX_QUOTED, RX_QUOTED_LENGTH, RX_QUOTED_JSON_START, RX_JSON, RX_UNQUOTED,
    RX_DROP_LINE, RX_DROP_RAW,
    RX_LOCKED
};

static const uint8_t prefix[] = "+QMTRECV:";

void ABoxMqttEc800Rx_Reset(ABoxMqttEc800Rx *rx)
{
    if (!rx || rx->in_callback) return;
    rx->state = RX_SEARCH;
    rx->prefix_length = rx->topic_quotes = rx->escaped = rx->in_string = 0U;
    rx->header_length = rx->topic_length = rx->payload_length = 0U;
    rx->json_depth = 0U;
    rx->expected = rx->remaining = 0U;
}

int ABoxMqttEc800Rx_Init(ABoxMqttEc800Rx *rx, uint8_t *header,
                         size_t header_capacity, uint8_t *topic,
                         size_t topic_capacity, uint8_t *payload,
                         size_t payload_capacity, uint32_t timeout_ms,
                         uint8_t legacy_json, ABoxMqttEc800MessageFn message,
                         void *user)
{
    if (!rx || !header || header_capacity < sizeof(prefix) + 8U ||
        !topic || !topic_capacity || !payload || !payload_capacity ||
        !timeout_ms || timeout_ms > INT32_MAX || !message || legacy_json > 1U) return 0;
    memset(rx, 0, sizeof(*rx));
    rx->header = header; rx->header_capacity = header_capacity;
    rx->topic = topic; rx->topic_capacity = topic_capacity;
    rx->payload = payload; rx->payload_capacity = payload_capacity;
    rx->timeout_ms = timeout_ms; rx->legacy_json = legacy_json;
    rx->message = message; rx->user = user;
    return 1;
}

static void reject(ABoxMqttEc800Rx *rx, uint8_t overflow, uint8_t drop_line)
{
    ++rx->counters.rejected;
    if (overflow) ++rx->counters.overflow;
    ABoxMqttEc800Rx_Reset(rx);
    if (drop_line) rx->state = RX_DROP_LINE;
}

static void deliver(ABoxMqttEc800Rx *rx)
{
    size_t topic_length = rx->topic_length;
    size_t payload_length = rx->payload_length;
    ABoxMqttEc800Rx_Reset(rx);
    ++rx->counters.completed;
    rx->in_callback = 1U;
    rx->message(rx->user, rx->topic, topic_length, rx->payload, payload_length);
    rx->in_callback = 0U;
}

static int parse_number(const uint8_t **cursor, const uint8_t *end)
{
    const uint8_t *p = *cursor;
    if (p == end || *p < '0' || *p > '9') return 0;
    while (p < end && *p >= '0' && *p <= '9') ++p;
    *cursor = p;
    return 1;
}

static int parse_header(ABoxMqttEc800Rx *rx)
{
    const uint8_t *p = rx->header + sizeof(prefix) - 1U;
    const uint8_t *end = rx->header + rx->header_length;
    if (end <= p) return 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (!parse_number(&p, end) || p == end || *p++ != ',') return 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (!parse_number(&p, end) || p == end || *p++ != ',') return 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p == end || *p++ != '"') return 0;
    while (p < end) {
        uint8_t c = *p++;
        if (c == '"') break;
        if (c == '\\') {
            if (p == end) return 0;
            c = *p++;
        }
        if (rx->topic_length >= rx->topic_capacity) return -1;
        rx->topic[rx->topic_length++] = c;
    }
    if (!rx->topic_length || p == end || *p != ',') return 0;
    return p + 1U == end;
}

static int append_payload(ABoxMqttEc800Rx *rx, uint8_t c)
{
    if (rx->payload_length >= rx->payload_capacity) {
        reject(rx, 1U, 1U);
        return 0;
    }
    rx->payload[rx->payload_length++] = c;
    return 1;
}

void ABoxMqttEc800Rx_Poll(ABoxMqttEc800Rx *rx, uint32_t now_ms)
{
    uint32_t elapsed;
    if (!rx || rx->in_callback) return;
    elapsed = now_ms - rx->last_byte_ms;
    /* A poll sampled before the last byte is behind it in modular tick order. */
    if ((rx->state != RX_SEARCH || rx->prefix_length) &&
        rx->state != RX_LOCKED &&
        elapsed <= INT32_MAX && elapsed >= rx->timeout_ms) {
        ++rx->counters.expired;
        ++rx->counters.rejected;
        if (rx->state == RX_SEARCH) ABoxMqttEc800Rx_Reset(rx);
        else rx->state = RX_LOCKED;
    }
}

int ABoxMqttEc800Rx_IsLocked(const ABoxMqttEc800Rx *rx)
{
    return rx && rx->state == RX_LOCKED;
}

int ABoxMqttEc800Rx_IsCollecting(const ABoxMqttEc800Rx *rx)
{
    return rx && (rx->state != RX_SEARCH || rx->prefix_length != 0U);
}

void ABoxMqttEc800Rx_Feed(ABoxMqttEc800Rx *rx, const uint8_t *bytes,
                          size_t length, uint32_t now_ms)
{
    size_t i;
    if (!rx || !bytes || rx->in_callback || rx->state == RX_LOCKED) return;
    ABoxMqttEc800Rx_Poll(rx, now_ms);
    if (rx->state == RX_LOCKED) return;
    for (i = 0U; i < length; ++i) {
        uint8_t c = bytes[i];
        rx->last_byte_ms = now_ms;
        if (rx->state == RX_SEARCH) {
            if (c == prefix[rx->prefix_length]) {
                if (++rx->prefix_length == sizeof(prefix) - 1U) {
                    memcpy(rx->header, prefix, sizeof(prefix) - 1U);
                    rx->header_length = sizeof(prefix) - 1U;
                    rx->prefix_length = 0U;
                    rx->state = RX_HEADER;
                }
            } else rx->prefix_length = (uint8_t)(c == prefix[0]);
            continue;
        }
        if (rx->state == RX_DROP_LINE) {
            if (c == '\n') ABoxMqttEc800Rx_Reset(rx);
            continue;
        }
        if (rx->state == RX_DROP_RAW) {
            if (--rx->remaining == 0U) ABoxMqttEc800Rx_Reset(rx);
            continue;
        }
        if (rx->state == RX_HEADER) {
            int parsed;
            if (c == '\n') { reject(rx, 0U, 0U); continue; }
            if (rx->header_length >= rx->header_capacity) {
                reject(rx, 1U, 1U); continue;
            }
            rx->header[rx->header_length++] = c;
            if (c == '"' && !rx->escaped) ++rx->topic_quotes;
            if (c == ',' && rx->topic_quotes == 2U) {
                parsed = parse_header(rx);
                if (parsed < 0) reject(rx, 1U, 1U);
                else if (!parsed) reject(rx, 0U, 1U);
                else rx->state = RX_AFTER_TOPIC;
                continue;
            }
            if (c == '\\' && !rx->escaped) rx->escaped = 1U;
            else rx->escaped = 0U;
            continue;
        }
        if (rx->state == RX_AFTER_TOPIC) {
            if (c == ' ' || c == '\t') continue;
            if (c >= '0' && c <= '9') {
                rx->expected = (uint32_t)(c - '0');
                rx->state = RX_LENGTH;
                continue;
            }
            if (c == '"') {
                if (!rx->legacy_json) { reject(rx, 0U, 1U); continue; }
                rx->state = RX_QUOTED; continue;
            }
            if (!rx->legacy_json) { reject(rx, 0U, 1U); continue; }
            if (rx->legacy_json && (c == '{' || c == '[')) {
                rx->state = RX_JSON; rx->json_depth = 1U;
            } else rx->state = RX_UNQUOTED;
            if (c == '\n') { deliver(rx); continue; }
            (void)append_payload(rx, c);
            continue;
        }
        if (rx->state == RX_LENGTH) {
            if (c >= '0' && c <= '9') {
                if (rx->expected > (UINT32_MAX - (uint32_t)(c - '0')) / 10U) {
                    reject(rx, 1U, 1U); continue;
                }
                rx->expected = rx->expected * 10U + (uint32_t)(c - '0');
                continue;
            }
            if (c == ',') {
                if (rx->expected > rx->payload_capacity) {
                    ++rx->counters.rejected; ++rx->counters.overflow;
                    rx->remaining = rx->expected;
                    rx->state = rx->legacy_json ? RX_LOCKED : RX_DROP_RAW;
                } else if (!rx->expected) deliver(rx);
                else rx->state = RX_LENGTH_START;
                continue;
            }
            reject(rx, 0U, 1U);
            continue;
        }
        if (rx->state == RX_LENGTH_START) {
            if (rx->legacy_json && c == '"') {
                rx->state = RX_QUOTED_LENGTH;
                continue;
            }
            rx->state = RX_RAW;
            rx->payload[rx->payload_length++] = c;
            if (rx->payload_length == rx->expected) deliver(rx);
            continue;
        }
        if (rx->state == RX_RAW) {
            rx->payload[rx->payload_length++] = c;
            if (rx->payload_length == rx->expected) deliver(rx);
            continue;
        }
        if (rx->state == RX_QUOTED || rx->state == RX_QUOTED_LENGTH) {
            uint8_t length_quoted = rx->state == RX_QUOTED_LENGTH;
            if (!rx->payload_length && (c == '{' || c == '[')) {
                rx->json_depth = 1U;
                rx->state = RX_QUOTED_JSON_START;
                (void)append_payload(rx, c);
                continue;
            }
            if (rx->escaped) {
                rx->escaped = 0U;
                if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c == 'b') c = '\b';
                else if (c == 'f') c = '\f';
            } else if (c == '\\') { rx->escaped = 1U; continue; }
            else if (c == '"') {
                if (length_quoted && rx->payload_length != rx->expected)
                    reject(rx, 0U, 1U);
                else deliver(rx);
                continue;
            }
            if (!append_payload(rx, c)) continue;
            if (length_quoted && rx->payload_length > rx->expected)
                reject(rx, 0U, 1U);
            continue;
        }
        if (rx->state == RX_QUOTED_JSON_START) {
            /* EC800 variants emit either escaped JSON or raw JSON inside
             * the outer quoted payload. The first byte after '{' or '['
             * distinguishes the forms without copying the full message. */
            if (c == '\\') {
                rx->state = rx->expected ? RX_QUOTED_LENGTH : RX_QUOTED;
                rx->escaped = 1U;
                continue;
            }
            rx->state = RX_JSON;
        }
        if (rx->state == RX_JSON) {
            if (rx->in_string) {
                if (rx->escaped) rx->escaped = 0U;
                else if (c == '\\') rx->escaped = 1U;
                else if (c == '"') rx->in_string = 0U;
            } else if (c == '"') rx->in_string = 1U;
            else if (c == '{' || c == '[') ++rx->json_depth;
            else if (c == '}' || c == ']') {
                if (rx->json_depth == 0U) { reject(rx, 0U, 1U); continue; }
                --rx->json_depth;
            }
            if (!append_payload(rx, c)) continue;
            if (rx->json_depth == 0U) {
                if (rx->expected && rx->payload_length != rx->expected)
                    reject(rx, 0U, 1U);
                else deliver(rx);
            }
            continue;
        }
        if (rx->state == RX_UNQUOTED) {
            if (c == '\n') {
                if (rx->payload_length && rx->payload[rx->payload_length - 1U] == '\r')
                    --rx->payload_length;
                deliver(rx);
            } else (void)append_payload(rx, c);
        }
    }
}
