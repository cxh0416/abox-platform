#ifndef ABOX_CLOUD_V4_H
#define ABOX_CLOUD_V4_H
#include "abox_mqtt_request.h"
#include "abox_mqtt_receipt.h"
#include "abox_cloud_service.h"

typedef struct {
    const char *name, *version;
    const char *const *instances;
    size_t instance_count;
} ABoxCloudV4Profile;
/* profiles and instances are sets. The encoder sorts without mutating input,
 * rejects duplicates, and hashes every field of the emitted Manifest data
 * other than manifestId. Fixed firmware capabilities may omit the ID. */
int ABoxCloudV4_ManifestId(char output[72], const char *firmware,
    const ABoxCloudV4Profile *profiles, size_t count);
int ABoxCloudV4_Manifest(char *out, size_t capacity, uint64_t timestamp,
    const char *firmware, const ABoxCloudV4Profile *profiles, size_t count, int with_id);
int ABoxCloudV4_Heartbeat(char *out, size_t capacity, uint64_t timestamp,
    const char *firmware, const char *manifest_id);
typedef struct {
    const char *firmware;
    const ABoxCloudV4Profile *profiles;
    size_t count;
    char manifest_id[72];
    uint32_t generation;
} ABoxCloudV4Capabilities;
/* Initialize to zero. Configuration strings/sets remain immutable until the
 * next Set. A new generation tells the existing Cloud bootstrap to republish. */
int ABoxCloudV4Capabilities_Set(ABoxCloudV4Capabilities *capabilities,
    const char *firmware, const ABoxCloudV4Profile *profiles, size_t count,
    int firmware_uniquely_defines_capabilities);
int ABoxCloudV4Capabilities_Manifest(const ABoxCloudV4Capabilities *capabilities,
    char *out, size_t capacity, uint64_t timestamp);
int ABoxCloudV4Capabilities_Heartbeat(const ABoxCloudV4Capabilities *capabilities,
    char *out, size_t capacity, uint64_t timestamp);
typedef struct {
    const char *report_id, *type, *name, *instance_id, *stream_id, *request_id;
    uint32_t sequence;
    uint64_t timestamp;
} ABoxCloudV4Report;
int ABoxCloudV4_Report(char *out, size_t capacity,
    const ABoxCloudV4Report *report, const cJSON *payload);
/* Enforce the enabled Profile and single/multi-instance membership too. */
int ABoxCloudV4_ReportRegistered(char *out, size_t capacity,
    const ABoxCloudV4Capabilities *capabilities, const ABoxMqttReportDescriptor *descriptor,
    const ABoxCloudV4Report *report, const cJSON *payload);
/* Returns the encoded code, or zero if even the fallback cannot fit. A large
 * result produces a complete 500 response, never a truncated object. */
uint16_t ABoxCloudV4_Response(char *out, size_t capacity, uint64_t timestamp,
    const char *request_id, uint16_t code, const char *message, const cJSON *result);

typedef struct { const char *report_name, *instance_id; } ABoxCloudV4StateKey;
typedef struct {
    char request_id[65];
    uint8_t digest[32];
    ABoxCloudV4StateKey *keys;
    size_t capacity, count;
    uint8_t valid;
} ABoxCloudV4Sync;
/* One plan per retained requestId. Key strings must remain immutable for its
 * lifetime. Only keys are frozen; retries obtain fresh business snapshots.
 * Different requestIds need another plan or an explicit owner-controlled Clear. */
int ABoxCloudV4Sync_Init(ABoxCloudV4Sync *sync, ABoxCloudV4StateKey *keys, size_t capacity);
void ABoxCloudV4Sync_Clear(ABoxCloudV4Sync *sync);
uint16_t ABoxCloudV4Sync_Begin(ABoxCloudV4Sync *sync, const char *request_id,
    const uint8_t digest[32], const ABoxCloudV4StateKey *keys, size_t count);
int ABoxCloudV4Sync_Result(const ABoxCloudV4Sync *sync, char *out, size_t capacity);
int ABoxCloudV4Sync_Queue(const ABoxCloudV4Sync *sync, ABoxCloudService *service);

typedef struct {
    void *context;
    /* Target supplies a fresh boot/stream nonce (RNG or durable boot source).
     * UID or unsynchronized UTC alone does not meet cross-boot uniqueness. */
    int (*nonce)(void *, uint8_t output[16]);
} ABoxCloudV4IdPort;
typedef struct {
    ABoxCloudV4IdPort port;
    uint8_t nonce[16];
    uint64_t counter;
    uint8_t valid;
} ABoxCloudV4Ids;
int ABoxCloudV4Ids_Init(ABoxCloudV4Ids *ids, const ABoxCloudV4IdPort *port);
int ABoxCloudV4Ids_Next(ABoxCloudV4Ids *ids, char output[97]);
typedef struct { char id[65]; uint32_t sequence; } ABoxCloudV4Stream;
int ABoxCloudV4Stream_Next(ABoxCloudV4Stream *stream, ABoxCloudV4Ids *ids);
/* Caller-owned immutable wire packet: retain through failed/uncertain receipts
 * and reconnects. A fresh logical observation must be explicitly queued. */
typedef struct {
    char *bytes;
    size_t capacity, length;
    uint64_t operation;
    uint32_t generation;
    uint8_t pending;
} ABoxCloudV4Packet;
int ABoxCloudV4Packet_Queue(ABoxCloudV4Packet *packet, const char *bytes, size_t length);
int ABoxCloudV4Packet_Begin(ABoxCloudV4Packet *packet, uint32_t generation, uint64_t operation);
int ABoxCloudV4Packet_Receipt(ABoxCloudV4Packet *packet, uint32_t generation,
    uint64_t operation, ABoxMqttReceiptStatus status);
void ABoxCloudV4Packet_Disconnected(ABoxCloudV4Packet *packet);
#endif
