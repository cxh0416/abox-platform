#ifndef ABOX_EC800_UFS_AT_H
#define ABOX_EC800_UFS_AT_H
#include "abox_ec800_at.h"
#include "abox_ec800_ufs.h"
/* One registered owner, caller buffers, no heap. Poll alongside At_Task.
 * Acquire excludes OTA/other file users for the complete open/raw/close
 * operation. Refusal is temporary and does not reset or stop MQTT. */
typedef struct {
    void *context;
    int (*acquire)(void *);
    void (*release)(void *);
} ABoxUfsLeasePort;
typedef struct {
    ABoxEc800At *at;
    ABoxUfsLeasePort lease;
    ABoxUfsRequest request;
    char path[64];
    uint64_t operation;
    uint32_t transferred, chunk_start, chunk_size;
    uint16_t handle, cme;
    uint8_t owner, state, submitted, done, ok, handle_seen, raw_seen, write_seen, leased;
    uint8_t short_read, transfer_failed;
    ABoxAsyncStatus status;
} ABoxEc800UfsAt;
int ABoxEc800UfsAt_Init(ABoxEc800UfsAt *backend, ABoxEc800At *at,
    ABoxEc800Owner owner, const ABoxUfsLeasePort *lease);
ABoxEc800UfsPort ABoxEc800UfsAt_Port(ABoxEc800UfsAt *backend);
void ABoxEc800UfsAt_Poll(ABoxEc800UfsAt *backend);
#endif
