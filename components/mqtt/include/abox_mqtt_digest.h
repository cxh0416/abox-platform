#ifndef ABOX_MQTT_DIGEST_H
#define ABOX_MQTT_DIGEST_H
#include "cJSON.h"
#include <stddef.h>
#include <stdint.h>

typedef int (*ABoxCanonicalWriter)(void *context, const char *bytes, size_t length);
/* Integer-only RFC 8785 subset. No allocation or full canonical buffer.
 * Profile collection normalization belongs to the caller; ordinary array order
 * is preserved. Depth <= 16; invalid UTF-8, duplicate keys and unsafe numbers fail.
 * A writer can receive partial output on failure and must discard that output. */
int ABoxMqttDigest_Canonical(const cJSON *value, ABoxCanonicalWriter write, void *context);
/* Wire parser also rejects exponent/fraction/-0 and embedded NUL (including
 * \u0000, which cannot be represented losslessly by cJSON strings).
 * Caller owns the returned tree; NULL means invalid input/allocation failure. */
cJSON *ABoxMqttDigest_Parse(const char *json, size_t length);
int ABoxMqttDigest_Value(const cJSON *value, uint8_t digest[32]);
int ABoxMqttDigest_RequestFields(const char *command, const cJSON *params,
                               const cJSON *expires_at, uint8_t digest[32]);
/* Hash ONLY {command, params, optional expiresAt}; neither requestId nor outer
 * timestamp participates. normalized_params=NULL uses data.params. */
int ABoxMqttDigest_Request(const cJSON *data, const cJSON *normalized_params,
                         uint8_t digest[32]);
#endif
