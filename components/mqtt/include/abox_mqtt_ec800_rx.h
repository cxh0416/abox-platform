#ifndef ABOX_MQTT_EC800_RX_H
#define ABOX_MQTT_EC800_RX_H

#include <stddef.h>
#include <stdint.h>

/* Feed UART bytes only from the UART/AT owner while MQTT owns the input;
 * OTA RAW bytes must bypass this parser. All storage belongs to the caller.
 * The callback must not call Feed or Reset recursively. After a partial-frame
 * timeout the parser locks until the owner drains/resynchronizes input and
 * explicitly calls Reset. */
typedef void (*ABoxMqttEc800MessageFn)(void *user, const uint8_t *topic,
                                        size_t topic_length, const uint8_t *payload,
                                        size_t payload_length);

typedef struct {
    uint32_t completed, rejected, overflow, expired;
} ABoxMqttEc800RxCounters;

typedef struct {
    uint8_t *header, *topic, *payload;
    size_t header_capacity, topic_capacity, payload_capacity;
    ABoxMqttEc800MessageFn message;
    void *user;
    uint32_t timeout_ms, last_byte_ms, expected, remaining;
    size_t header_length, topic_length, payload_length;
    ABoxMqttEc800RxCounters counters;
    uint8_t state, prefix_length, topic_quotes, escaped, in_string;
    uint16_t json_depth;
    uint8_t legacy_json, in_callback;
} ABoxMqttEc800Rx;

/* Timeout is 1..INT32_MAX ms. Feed/Poll use the same wrapping tick, with
 * poll intervals below half the tick range. */
int ABoxMqttEc800Rx_Init(ABoxMqttEc800Rx *rx, uint8_t *header,
                         size_t header_capacity, uint8_t *topic,
                         size_t topic_capacity, uint8_t *payload,
                         size_t payload_capacity, uint32_t timeout_ms,
                         uint8_t legacy_json, ABoxMqttEc800MessageFn message,
                         void *user);
void ABoxMqttEc800Rx_Feed(ABoxMqttEc800Rx *rx, const uint8_t *bytes,
                          size_t length, uint32_t now_ms);
void ABoxMqttEc800Rx_Reset(ABoxMqttEc800Rx *rx);
void ABoxMqttEc800Rx_Poll(ABoxMqttEc800Rx *rx, uint32_t now_ms);
int ABoxMqttEc800Rx_IsLocked(const ABoxMqttEc800Rx *rx);
int ABoxMqttEc800Rx_IsCollecting(const ABoxMqttEc800Rx *rx);

#endif
