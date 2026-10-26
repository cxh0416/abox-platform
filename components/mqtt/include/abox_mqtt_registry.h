#ifndef ABOX_MQTT_REGISTRY_H
#define ABOX_MQTT_REGISTRY_H
#include "abox_mqtt_request.h"

#define ABOX_MQTT_MATCHER_MAX_DEPTH 8U
#define ABOX_MQTT_MATCHER_MAX_NODES 64U
typedef enum { ABOX_MATCH_EQUALS, ABOX_MATCH_ALL, ABOX_MATCH_ANY } ABoxMqttMatcherKind;
typedef enum { ABOX_MATCH_STRING, ABOX_MATCH_INTEGER, ABOX_MATCH_BOOLEAN,
    ABOX_MATCH_NULL, ABOX_MATCH_NUMBER } ABoxMqttMatcherValueType;
typedef struct {
    ABoxMqttMatcherValueType type;
    union { const char *string; int64_t integer; uint8_t boolean; double number; } value;
} ABoxMqttMatcherValue;
typedef struct ABoxMqttMatcher {
    ABoxMqttMatcherKind kind;
    const char *field; /* data.command, data.params, or dotted object keys under data.params. */
    ABoxMqttMatcherValue equals;
    const struct ABoxMqttMatcher *children;
    size_t count;
} ABoxMqttMatcher;
int ABoxMqttMatcher_Valid(const ABoxMqttMatcher *matcher);
int ABoxMqttMatcher_Matches(const ABoxMqttMatcher *matcher, const ABoxMqttRequest *request);

/* This is the production operation table. Resolve and Describe both read its
 * runtime policy and the SAME declarative matcher. runtime.matches must be
 * NULL; custom side-effect-free business validation remains supported. */
typedef struct {
    ABoxMqttOperation runtime;
    const ABoxMqttMatcher *matcher; /* NULL: whole command. */
    const char *introduced_version, *semantic_description;
    const char *params_schema, *result_schema; /* JSON Schema object/boolean; NULL result means absent. */
    const char *success_boundary, *reboot_policy;
    const uint16_t *error_codes;
    size_t error_count;
} ABoxMqttOperationContract;
typedef struct {
    const char *name, *introduced_version, *semantic_description, *payload_schema;
    uint8_t full_state, instance_required, strict_order, request_correlation;
} ABoxMqttReportContract;
typedef struct {
    const ABoxMqttProfileDescriptor *descriptor;
    const char *contract;
    const uint16_t *supported_majors;
    size_t supported_major_count;
    const ABoxMqttOperationContract *operations;
    size_t operation_count;
    const ABoxMqttReportContract *reports;
    size_t report_count;
} ABoxMqttProfileContract;
typedef struct {
    const ABoxMqttProfileContract *const *profiles;
    size_t count;
} ABoxMqttRegistryContract;

/* Structural consistency with the legacy C descriptors, parameter category
 * projection, expiry policy, report coverage and reserved Core names. JSON
 * Schema syntax is checked by Describe/offline tooling, not executed on MCU. */
int ABoxMqttRegistry_Valid(const ABoxMqttRegistryContract *registry);
uint16_t ABoxMqttRegistry_Resolve(const ABoxMqttRegistryContract *registry,
    const ABoxMqttRequest *request, const ABoxMqttOperation **operation);
/* Host/diagnostic streaming snapshot of the compiled production contracts.
 * Output is Registry metadata (not a public MQTT payload); no 1024-byte cap.
 * A failed writer/schema invalidates the entire partial output. */
int ABoxMqttRegistry_Describe(const ABoxMqttRegistryContract *registry,
    ABoxCanonicalWriter write, void *context);
#endif
