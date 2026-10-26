#ifndef ABOX_MQTT_EC800_H
#define ABOX_MQTT_EC800_H

#include "abox_ec800_at.h"
#include "abox_mqtt_ec800_rx.h"
#include "abox_mqtt_backoff.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ABOX_MQTT_EC800_IDLE,
    ABOX_MQTT_EC800_CONFIGURING,
    ABOX_MQTT_EC800_OPENING,
    ABOX_MQTT_EC800_CONNECTING,
    ABOX_MQTT_EC800_SUBSCRIBING,
    ABOX_MQTT_EC800_READY,
    ABOX_MQTT_EC800_CLOSING,
    ABOX_MQTT_EC800_RETRY_WAIT,
    ABOX_MQTT_EC800_PAUSED,
    ABOX_MQTT_EC800_BLOCKED
} ABoxMqttEc800State;

typedef enum {
    ABOX_MQTT_EC800_PUBLISH_SUBMITTED,
    ABOX_MQTT_EC800_PUBLISH_CONFIRMED,
    ABOX_MQTT_EC800_PUBLISH_FAILED
} ABoxMqttEc800PublishEvent;

typedef struct {
    const char *apn, *host, *client_id, *username, *password;
    uint16_t port;
    const char *const *subscriptions;
    uint8_t subscription_count, client_index;
    uint32_t command_timeout_ms, open_timeout_ms, connect_timeout_ms;
    uint32_t subscribe_timeout_ms, publish_timeout_ms, retry_delay_ms;
    uint8_t legacy_json;
    /* Zero leaves the modem setting unchanged. */
    uint8_t mqtt_version, pdp_context_id;
} ABoxMqttEc800Config;

typedef struct {
    uint8_t *header, *topic, *payload;
    size_t header_capacity, topic_capacity, payload_capacity;
    uint8_t *publish_payload;
    size_t publish_capacity;
} ABoxMqttEc800Buffers;

typedef struct {
    void *user;
    void (*message)(void *, const uint8_t *, size_t, const uint8_t *, size_t);
    void (*state_changed)(void *, ABoxMqttEc800State);
    void (*publish_event)(void *, uint64_t, ABoxMqttEc800PublishEvent);
    void (*modem_reset)(void *);
} ABoxMqttEc800Callbacks;

typedef struct {
    uint32_t command_failed, urc_rejected, publish_timeout, stale_publish;
    uint32_t disconnects, reconnects;
    ABoxMqttEc800RxCounters receive;
} ABoxMqttEc800Counters;

typedef struct {
    ABoxEc800At *at;
    ABoxMqttEc800Rx rx;
    ABoxMqttEc800Config config;
    ABoxMqttEc800Buffers buffers;
    ABoxMqttEc800Callbacks callbacks;
    ABoxMqttEc800Counters counters;
    ABoxMqttEc800State state;
    uint64_t generation, publish_operation, next_operation;
    uint32_t entered_ms, command_done_ms, last_publish_ms;
    uint16_t publish_msg_id, next_msg_id, publish_length;
    uint16_t next_sub_msg_id, pending_sub_msg_id;
    uint8_t step, subscription_index, command_pending, command_result;
    uint8_t urc_pending, urc_result, publish_active, publish_prompt;
    uint8_t enabled, paused, security_ready, workspace_borrowed;
    uint8_t close_requested, initialized, registered, attached, pdp_active;
    uint8_t close_step;
    ABoxMqttBackoff backoff;
    uint32_t retry_wait_ms;
    uint8_t v4_backoff, backoff_started;
} ABoxMqttEc800;

/* The caller owns every config string and buffer for the instance lifetime.
 * Poll and callbacks run in the UART owner's task, never concurrently. */
int ABoxMqttEc800_Init(ABoxMqttEc800 *, ABoxEc800At *,
                        const ABoxMqttEc800Config *,
                        const ABoxMqttEc800Buffers *,
                        const ABoxMqttEc800Callbacks *, uint32_t now_ms);
/* Update the endpoint and subscriptions only after a complete stop. */
int ABoxMqttEc800_Configure(ABoxMqttEc800 *,
                             const ABoxMqttEc800Config *);
void ABoxMqttEc800_Poll(ABoxMqttEc800 *, uint32_t now_ms);
int ABoxMqttEc800_Start(ABoxMqttEc800 *, uint32_t now_ms);
/* Opt in once after Init, before Start. Legacy consumers keep their frozen
 * fixed interval; V4 consumers get the shared jitter/backoff policy. */
int ABoxMqttEc800_UseV4Backoff(ABoxMqttEc800 *, uint32_t device_boot_seed);
void ABoxMqttEc800_RequestReconnect(ABoxMqttEc800 *, uint32_t now_ms);
void ABoxMqttEc800_SetSecurityReady(ABoxMqttEc800 *, uint8_t ready);
/* Runtime gate: prevents delivery and publish while runtime coordinates stop.
 * It does not itself send AT commands. */
void ABoxMqttEc800_SetPausedSoft(ABoxMqttEc800 *, uint8_t paused);
/* Returns 1 only after the client is closed and AT is drained; -1 means
 * modem reset is required before reuse. */
int ABoxMqttEc800_Stop(ABoxMqttEc800 *, uint32_t now_ms);
int ABoxMqttEc800_Pause(ABoxMqttEc800 *, uint32_t now_ms);
int ABoxMqttEc800_Resume(ABoxMqttEc800 *, uint32_t now_ms);
void ABoxMqttEc800_OnModemReset(ABoxMqttEc800 *, uint32_t now_ms);
int ABoxMqttEc800_Publish(ABoxMqttEc800 *, const char *topic,
                           const uint8_t *payload, size_t length,
                           uint8_t qos, uint8_t retain, uint64_t *operation);
void ABoxMqttEc800_CancelPublish(ABoxMqttEc800 *);
/* Borrow is permitted only after Pause has completed. Return requires that
 * the borrower has stopped writing; it resets the receive parser. */
uint8_t *ABoxMqttEc800_BorrowWorkspace(ABoxMqttEc800 *, size_t *capacity);
int ABoxMqttEc800_ReturnWorkspace(ABoxMqttEc800 *);
ABoxMqttEc800State ABoxMqttEc800_GetState(const ABoxMqttEc800 *);
int ABoxMqttEc800_IsReady(const ABoxMqttEc800 *);
int ABoxMqttEc800_IsConnected(const ABoxMqttEc800 *);
void ABoxMqttEc800_GetCounters(const ABoxMqttEc800 *, ABoxMqttEc800Counters *);

#ifdef __cplusplus
}
#endif
#endif
