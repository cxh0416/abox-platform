#ifndef ABOX_MQTT_IDEM_UFS_H
#define ABOX_MQTT_IDEM_UFS_H
#include "abox_mqtt_idempotency.h"
#include "abox_ufs_journal.h"
typedef struct {
    void *context;
    int (*initialized)(void *);
    int (*mark_initialized)(void *);
    int (*acquire)(void *);
    void (*release)(void *);
} ABoxMqttIdemUfsPort;
typedef struct {
    ABoxMqttIdempotency cache;
    ABoxUfsJournal journal;
    ABoxMqttIdemUfsPort port;
    uint64_t mono;
    uint8_t loaded, loading;
} ABoxMqttIdemUfs;
/* Each device namespace owns its paths and durable first-use marker. This is
 * additive: existing product file ABIs require an explicit product migration. */
int ABoxMqttIdemUfs_Init(ABoxMqttIdemUfs *store, const char *device_id,
    ABoxMqttIdemEntry *entries, size_t count, uint8_t *responses, size_t response_capacity,
    const ABoxEc800UfsPort *files, const ABoxMqttIdemUfsPort *resources,
    const char *path_a, const char *path_b, uint8_t *workspace, size_t workspace_size);
int ABoxMqttIdemUfs_Load(ABoxMqttIdemUfs *store, uint64_t mono_ms);
void ABoxMqttIdemUfs_Poll(ABoxMqttIdemUfs *store, uint64_t mono_ms);
void ABoxMqttIdemUfs_OnModemReset(ABoxMqttIdemUfs *store);
#endif
