#ifndef ABOX_MQTT_DIGEST_H
#define ABOX_MQTT_DIGEST_H
#include "cJSON.h"
#include <stddef.h>
#include <stdint.h>

typedef int (*ABoxCanonicalWriter)(void *context, const char *bytes, size_t length);
typedef int (*ABoxCanonicalEncoder)(void *value, ABoxCanonicalWriter write, void *context);
/* Profile adapters emit their normalized params with these streaming primitives.
 * They must emit one complete canonical JSON value, with keys already sorted.
 * No product adapter owns the request envelope or SHA-256 lifecycle. */
int ABoxMqttDigest_WriteString(const char *value, ABoxCanonicalWriter write, void *context);
int ABoxMqttDigest_WriteInteger(int64_t value, ABoxCanonicalWriter write, void *context);
/* Exact fixed-point codec for EXISTING Profile milli-unit fields; 1000 -> 1,
 * -200 -> -0.2. Does not admit fractions in the public wire parser/cJSON codec. */
int ABoxMqttDigest_WriteMilli(int32_t value, ABoxCanonicalWriter write, void *context);
int ABoxMqttDigest_WriteRequest(const char *command, int has_expires_at, uint64_t expires_at,
    ABoxCanonicalEncoder params, void *value, ABoxCanonicalWriter write, void *context);
int ABoxMqttDigest_RequestStream(const char *command, int has_expires_at, uint64_t expires_at,
    ABoxCanonicalEncoder params, void *value, uint8_t digest[32]);
int ABoxMqttDigest_NullRequest(const char *command, int has_expires_at,
                              uint64_t expires_at, uint8_t digest[32]);
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
