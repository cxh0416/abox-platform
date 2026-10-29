#ifndef ABOX_MAINTENANCE_SERVICE_H
#define ABOX_MAINTENANCE_SERVICE_H
#include "abox_mqtt_trial_executor.h"
#include "abox_config_store.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The caller's trial/executor are the only state machine and action queue.
 * Actions execute in the communication owner. 1 means accepted asynchronous
 * work, 0 means refusal. Product ports own transport and persistence codecs. */
typedef struct {
    void *context;
    int (*action)(void *, ABoxMqttTrialExecutorAction, uint64_t,
                  const ABoxMqttConfig *, uint32_t);
} ABoxMaintenanceServicePort;
/* V4 maintenance accepts authenticated TLS active and candidate only.
 * This is a policy gate over the existing trial, not a separate state machine. */
int ABoxMaintenanceService_StartTls(ABoxMqttTrial *trial,
    const ABoxMqttConfig *active, const ABoxMqttConfig *candidate,
    uint64_t proof, uint32_t now, uint32_t timeout, uint32_t restore_timeout);
int ABoxMaintenanceService_Poll(ABoxMqttTrial *trial,
    ABoxMqttTrialExecutor *executor, const ABoxMaintenanceServicePort *port,
    uint32_t now);
/* Saves and verifies the candidate, reporting exactly one trial event. */
ABoxConfigStoreResult ABoxMaintenanceService_Commit(ABoxMqttTrial *trial,
    uint64_t session, const ABoxConfigStorePort *store,
    const void *candidate, const void *stable, uint32_t now);
int ABoxMaintenanceService_Busy(const ABoxMqttTrial *trial);
/* For runtime ports whose connect/subscribe/verify actions are already driven
 * by the runtime. Consume only the matching no-op action in this owner event
 * so a fast readiness/proof event cannot race the next poll. Returns -1 for
 * an unrelated state, otherwise the delivered trial event. */
int ABoxMaintenanceService_RuntimeEvent(ABoxMqttTrial *trial,
    ABoxMqttTrialExecutor *executor, ABoxMqttRuntimeState state, uint32_t now);
#ifdef __cplusplus
}
#endif
#endif
