#ifndef ABOX_TIME_CLIENT_H
#define ABOX_TIME_CLIENT_H
#include "abox_time_sync.h"
#include <stddef.h>
typedef struct {
    void *context;
    int (*send)(void *, const char *topic, const char *packet, size_t length);
    int (*apply)(void *, uint64_t utc, uint32_t error, uint64_t sent, uint64_t received);
} ABoxTimeClientPort;
typedef struct {
    ABoxTimeSync sampling;
    ABoxTimeClientPort port;
    /* 32-byte VID plus the longest fixed time topic and NUL. */
    char response_topic[72];
    uint64_t seed, utc;
    uint32_t generation;
    uint8_t ready;
} ABoxTimeClient;
/* Prefix is only for correlation, not freshness or authentication.
 * Zero selects the shared default; IDs may repeat after device reset. */
int ABoxTimeClient_Init(ABoxTimeClient *client, const char *device_id,
    uint64_t boot_seed, const ABoxTimeClientPort *port);
int ABoxTimeClient_SetDevice(ABoxTimeClient *client, const char *device_id);
/* authenticated_ready means an established server-authenticated connection.
 * Reconfiguration/disconnect must be passed even while no packet can be sent. */
void ABoxTimeClient_Poll(ABoxTimeClient *client, int authenticated_ready,
    uint32_t generation, uint64_t mono_ms, uint64_t utc_ms);
int ABoxTimeClient_Message(ABoxTimeClient *client, const char *topic,
    const char *packet, size_t length, int authenticated_ready,
    uint32_t generation, uint64_t received_mono);
#endif
