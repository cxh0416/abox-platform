#ifndef ABOX_MQTT_IDEMPOTENCY_H
#define ABOX_MQTT_IDEMPOTENCY_H
#include "abox_mqtt_v4.h"
typedef enum {
    ABOX_IDEM_EMPTY, ABOX_IDEM_PREPARING, ABOX_IDEM_PREPARED,
    ABOX_IDEM_DISPATCHED, ABOX_IDEM_FINALIZING, ABOX_IDEM_FINAL,
    ABOX_IDEM_INTERRUPTED
} ABoxMqttIdemState;
typedef struct {
    char request_id[65];
    uint8_t digest[32];
    uint64_t expires_at, protect_mono;
    uint16_t response_length;
    uint8_t state;
} ABoxMqttIdemEntry;
typedef struct {
    void *context;
    /* -1 failure, 0 resource busy, 1 started. poll: -1 failed, 0 pending,
     * 1 durable and readback verified. commit serializes the CURRENT table
     * using Encode into the storage owner's borrowed buffer. */
    int (*commit)(void *);
    int (*poll)(void *);
    int (*loaded)(void *); /* Both startup copies have been inspected/recovered. */
} ABoxMqttIdemPort;
typedef enum { ABOX_IDEM_NEW, ABOX_IDEM_WAIT, ABOX_IDEM_REPLAY,
    ABOX_IDEM_CONFLICT, ABOX_IDEM_UNCERTAIN, ABOX_IDEM_UNAVAILABLE } ABoxMqttIdemMatch;
/* Per-record storage adapter. Handles are opaque to product code. */
typedef struct {
    ABoxMqttIdemMatch (*find)(void *, const char *, const uint8_t *, size_t *);
    uint16_t (*begin)(void *, const char *, const uint8_t *, uint64_t, const ABoxClockSnapshot *, size_t *);
    void (*poll)(void *);
    uint16_t (*take)(void *, size_t, const ABoxClockSnapshot *);
    int (*complete)(void *, size_t, const char *, size_t);
    const char *(*response)(void *, size_t, size_t *);
    void (*invalidate)(void *);
    ABoxMqttIdemState (*state)(void *, size_t);
} ABoxMqttIdemBackend;

typedef struct {
    ABoxMqttIdemEntry *entries;
    uint8_t *responses;
    size_t count, response_capacity, active;
    const char *device_id;
    ABoxMqttIdemPort port;
    uint8_t ready, failed, committing;
    const ABoxMqttIdemBackend *backend;
    void *backend_context;
} ABoxMqttIdempotency;
int ABoxMqttIdem_Init(ABoxMqttIdempotency *cache, const char *device_id,
    ABoxMqttIdemEntry *entries, size_t count, uint8_t *responses,
    size_t response_capacity, const ABoxMqttIdemPort *port);
/* Only storage may call Restore after validating its checksum/version. Empty
 * data requires independently proven first use. PREPARED never resumes. */
int ABoxMqttIdem_Restore(ABoxMqttIdempotency *cache, const uint8_t *data,
    size_t length, uint64_t mono_ms);
int ABoxMqttIdem_Encode(const ABoxMqttIdempotency *cache, uint8_t *out,
    size_t capacity, size_t *length);
ABoxMqttIdemMatch ABoxMqttIdem_Find(const ABoxMqttIdempotency *cache,
    const char *request_id, const uint8_t digest[32], size_t *index);
uint16_t ABoxMqttIdem_Begin(ABoxMqttIdempotency *cache, const char *request_id,
    const uint8_t digest[32], uint64_t expires_at,
    const ABoxClockSnapshot *clock, size_t *index);
void ABoxMqttIdem_Poll(ABoxMqttIdempotency *cache);
/* A successful call grants dispatch ONCE, only after durable reservation.
 * Failure after preparation should be completed with its own saved response. */
uint16_t ABoxMqttIdem_TakeExecutable(ABoxMqttIdempotency *cache, size_t index,
    const ABoxClockSnapshot *clock);
int ABoxMqttIdem_Complete(ABoxMqttIdempotency *cache, size_t index,
    const char *complete_response, size_t length);
const char *ABoxMqttIdem_Response(const ABoxMqttIdempotency *cache, size_t index,
    size_t *length);
/* Called on uncertain storage/reset before reloading. Completed RAM responses
 * remain replayable, but no new action or dispatch is allowed. */
void ABoxMqttIdem_Invalidate(ABoxMqttIdempotency *cache);
/* Inspect a handle returned by Begin or Find. Copy any borrowed response
 * before the next cache operation. */
ABoxMqttIdemState ABoxMqttIdem_State(const ABoxMqttIdempotency *cache, size_t index);
#endif
