#ifndef ABOX_MQTT_REQUEST_H
#define ABOX_MQTT_REQUEST_H
#include "abox_mqtt_digest.h"
#include "abox_mqtt_v4.h"

#define ABOX_MQTT_V4_PACKET_LIMIT 1024U
#define ABOX_MQTT_V4_VALUE_LIMIT 768U
typedef enum { ABOX_MQTT_ID_DEVICE, ABOX_MQTT_ID_REQUEST,
    ABOX_MQTT_ID_REPORT, ABOX_MQTT_ID_STREAM, ABOX_MQTT_ID_INSTANCE } ABoxMqttIdKind;
int ABoxMqttV4_IdValid(const char *value, ABoxMqttIdKind kind);
int ABoxMqttV4_NameValid(const char *value);
int ABoxMqttV4_ProfileVersionValid(const char *value);

typedef struct {
    cJSON *root;
    const cJSON *params;
    const char *request_id, *command;
    uint64_t timestamp, expires_at;
    uint8_t digest[32], has_expires_at;
} ABoxMqttRequest;
/* A failed decode may retain a trusted request_id for an error response.
 * Close on every path; do not reuse strings after Close. Caller supplies an
 * empty context (or closes the previous request) before Decode. */
uint16_t ABoxMqttRequest_Decode(ABoxMqttRequest *request, const char *json,
    size_t length, ABoxMqttNumberField number_field, void *context);
void ABoxMqttRequest_Close(ABoxMqttRequest *request);
/* Apply only normalization explicitly defined by the Profile (for example a
 * set of targets). Ordinary arrays retain order. No second JSON tree needed. */
int ABoxMqttRequest_Normalize(ABoxMqttRequest *request,
    ABoxCanonicalEncoder normalized_params, void *context);
typedef enum { ABOX_MQTT_EXPIRY_FORBIDDEN, ABOX_MQTT_EXPIRY_OPTIONAL,
    ABOX_MQTT_EXPIRY_REQUIRED } ABoxMqttExpiryPolicy;
typedef struct {
    const char *command, *profile;
    ABoxMqttCommandClass classification;
    ABoxMqttExpiryPolicy expiry;
    uint32_t max_validity_ms;
    int (*matches)(void *, const cJSON *params);
    uint16_t (*validate)(void *, const cJSON *params);
    void *context;
} ABoxMqttOperation;
/* Core sync_state is handled here and cannot be overridden by a Profile.
 * Descriptor callbacks only inspect params; they must have no side effects. */
uint16_t ABoxMqttRequest_Resolve(const ABoxMqttRequest *request,
    const ABoxMqttOperation *operations, size_t count,
    const ABoxMqttOperation **operation);
/* Call only for a new request, AFTER idempotency lookup. Recheck after an
 * asynchronous durable reservation and immediately before dispatch. */
uint16_t ABoxMqttRequest_CheckNew(const ABoxMqttRequest *request,
    const ABoxMqttOperation *operation, const ABoxClockSnapshot *clock);
#endif
