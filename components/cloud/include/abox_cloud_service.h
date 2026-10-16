#ifndef ABOX_CLOUD_SERVICE_H
#define ABOX_CLOUD_SERVICE_H

#include <stdint.h>
#include "abox_mqtt_receipt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One communication owner calls these functions. The product owns payload
 * encoding and transport; this service owns common ordering and tracking. */
typedef enum {
    ABOX_CLOUD_JOB_NONE = 0,
    ABOX_CLOUD_JOB_MANIFEST,
    ABOX_CLOUD_JOB_HEARTBEAT,
    ABOX_CLOUD_JOB_STATE,
    ABOX_CLOUD_JOB_RESPONSE,
    ABOX_CLOUD_JOB_PRODUCT_STATE, /* Opt-in product state deadline, before ordinary responses. */
    ABOX_CLOUD_JOB_EXTERNAL /* Product log/alert work below all Cloud jobs. */
} ABoxCloudJobKind;

typedef struct {
    ABoxCloudJobKind kind;
    uint32_t generation;
    uint8_t state_index;
    const char *reason;       /* For state: startup, reconnect, sync_state, event, periodic. */
    const char *request_id;   /* For response and sync_state; valid until next mutation. */
} ABoxCloudJob;

/* Minimal bootstrap cursor for products whose existing TX scheduler still
 * owns responses and periodic reports. It needs no payload or request buffers. */
typedef struct {
    uint64_t operation;
    uint32_t generation;
    ABoxCloudJobKind in_flight;
    uint8_t ready;
    uint8_t ever_ready;
    uint8_t stage;
} ABoxCloudBootstrap;

int ABoxCloudBootstrap_Init(ABoxCloudBootstrap *bootstrap);
void ABoxCloudBootstrap_SetReady(ABoxCloudBootstrap *bootstrap, int ready);
void ABoxCloudBootstrap_NetworkChanged(ABoxCloudBootstrap *bootstrap);
int ABoxCloudBootstrap_Active(const ABoxCloudBootstrap *bootstrap);
int ABoxCloudBootstrap_Next(const ABoxCloudBootstrap *bootstrap, ABoxCloudJob *job);
int ABoxCloudBootstrap_Begin(ABoxCloudBootstrap *bootstrap,
                             const ABoxCloudJob *job, uint64_t operation);
ABoxCloudJobKind ABoxCloudBootstrap_Receipt(ABoxCloudBootstrap *bootstrap,
                                            uint32_t generation,
                                            uint64_t operation, int confirmed);

/* Compact tracker for product-owned TX schedulers. A receipt from an older
 * connection or a different publish cannot complete the current operation. */
typedef struct {
    uint64_t operation;
    uint32_t generation;
    uint8_t kind; /* Product-owned nonzero kind; zero means idle. */
    uint8_t ready;
} ABoxCloudTxTracker;

typedef enum {
    ABOX_CLOUD_TX_IGNORED = 0,
    ABOX_CLOUD_TX_CONFIRMED,
    ABOX_CLOUD_TX_FAILED
} ABoxCloudTxReceipt;

int ABoxCloudTxTracker_Init(ABoxCloudTxTracker *tracker);
void ABoxCloudTxTracker_NetworkChanged(ABoxCloudTxTracker *tracker);
void ABoxCloudTxTracker_SetReady(ABoxCloudTxTracker *tracker, int ready);
int ABoxCloudTxTracker_Begin(ABoxCloudTxTracker *tracker,
                             uint8_t kind, uint64_t operation);
ABoxCloudTxReceipt ABoxCloudTxTracker_Receipt(ABoxCloudTxTracker *tracker,
                                               uint32_t generation,
                                               uint64_t operation,
                                               int confirmed, uint8_t *kind);

typedef struct {
    uint32_t heartbeat_period_ms;
    uint32_t status_period_ms; /* Zero disables periodic state after bootstrap. */
    uint8_t response_preempts_bootstrap; /* Preserve a product's existing TX priority. */
} ABoxCloudServiceOptions;

typedef struct {
    ABoxCloudServiceOptions options;
    uint32_t generation;
    uint32_t heartbeat_due;
    uint32_t status_due;
    uint32_t event_serial;
    uint32_t in_flight_event_serial;
    uint32_t correlated_serial;
    uint32_t in_flight_correlated_serial;
    uint64_t operation;
    ABoxCloudJobKind in_flight;
    uint8_t ready;
    uint8_t state_count, state_index, product_state_pending;
    uint8_t ever_ready;
    uint8_t bootstrap_stage;
    uint8_t response_pending;
    uint8_t response_sync;
    uint8_t sync_pending;
    uint8_t event_pending;
    char response_request_id[65];
    char *pending_sync_request_id; /* Optional caller-owned 65-byte slot. */
    char sync_request_id[65];
    char correlated_reason[32];
    char event_reason[32];
} ABoxCloudService;

int ABoxCloudService_Init(ABoxCloudService *service,
                          const ABoxCloudServiceOptions *options, uint32_t now);
void ABoxCloudService_SetReady(ABoxCloudService *service, int ready, uint32_t now);
void ABoxCloudService_NetworkChanged(ABoxCloudService *service);
void ABoxCloudService_SetStateCount(ABoxCloudService *service, uint8_t count);
void ABoxCloudService_SetProductStatePending(ABoxCloudService *service, int pending);
int ABoxCloudService_Bootstrapping(const ABoxCloudService *service);
/* The caller retains its response topic/payload until RESPONSE confirmation. */
int ABoxCloudService_QueueResponse(ABoxCloudService *service,
                                   const char *request_id, int sync_state);
/* Retain a sync request while a previous response or correlated report owns
 * its slot. Next returns RESPONSE with this ID once that slot becomes free. */
int ABoxCloudService_QueueSyncRequest(ABoxCloudService *service, const char *request_id);
int ABoxCloudService_SetSyncStorage(ABoxCloudService *service, char *storage,
                                    unsigned capacity);
/* Product jobs share the same operation/generation slot and run only when
 * the common scheduler has no work. */
int ABoxCloudService_BeginExternal(ABoxCloudService *service,
                                   uint64_t operation, uint32_t now);
int ABoxCloudService_BeginExternalPriority(ABoxCloudService *service,
    uint64_t operation, uint32_t now, int preempt_periodic);
/* Dropping an expired product response also invalidates its old operation.
 * A late confirmation cannot finish a later response. */
void ABoxCloudService_CancelResponse(ABoxCloudService *service);
/* Queue one full state report correlated to an already accepted product
 * request. Replacing an older pending report matches legacy last-write-wins
 * behavior; an old in-flight receipt cannot clear the replacement. */
int ABoxCloudService_QueueStateReport(ABoxCloudService *service,
                                      const char *request_id, const char *reason);
int ABoxCloudService_WakeState(ABoxCloudService *service, const char *reason);
int ABoxCloudService_Next(ABoxCloudService *service, uint32_t now,
                          ABoxCloudJob *job);
int ABoxCloudService_Begin(ABoxCloudService *service, const ABoxCloudJob *job,
                           uint64_t operation, uint32_t now);
/* Returns the completed job kind only for a matching confirmed operation.
 * A failed operation clears the in-flight slot but retains queued work. */
ABoxCloudJobKind ABoxCloudService_Receipt(ABoxCloudService *service,
                                          uint32_t generation, uint64_t operation,
                                          int confirmed, uint32_t now);

/* Store this provider table in Flash. Payloads and records remain in caller
 * storage; publish returns an exact transport operation. External work may
 * return -1 to hold its priority while encoding or waiting for a resource. */
typedef struct {
    void *context;
    int (*ready)(void *);
    int (*publish_active)(void *);
    ABoxMqttReceiptStatus (*receipt)(void *, uint64_t);
    int (*publish)(void *, const ABoxCloudJob *, uint64_t *);
    int (*external)(void *, uint64_t *);
    void (*completed)(void *, ABoxCloudJobKind);
    void (*failed)(void *);
    uint8_t external_preempts_periodic;
} ABoxCloudServicePort;
typedef enum { ABOX_CLOUD_POLL_BLOCKED, ABOX_CLOUD_POLL_IDLE,
               ABOX_CLOUD_POLL_SUBMITTED } ABoxCloudPollResult;
ABoxCloudPollResult ABoxCloudService_Poll(ABoxCloudService *service,
    const ABoxCloudServicePort *port, uint32_t now);

#ifdef __cplusplus
}
#endif
#endif
