#include "abox_maintenance_service.h"
#include <string.h>
int ABoxMaintenanceService_StartTls(ABoxMqttTrial *trial,
    const ABoxMqttConfig *active, const ABoxMqttConfig *candidate,
    uint64_t proof, uint32_t now, uint32_t timeout, uint32_t restore_timeout)
{
    if (!active || !candidate || active->tls_enabled != 1U || candidate->tls_enabled != 1U ||
        !ABoxTlsCredentials_Valid(&active->tls_credentials) ||
        !ABoxTlsCredentials_Valid(&candidate->tls_credentials) ||
        active->tls_credentials.mode != candidate->tls_credentials.mode) return 0;
    if (active->tls_credentials.mode == ABOX_TLS_AUTH_PSK &&
        (active->tls_credentials.generation != candidate->tls_credentials.generation ||
         strncmp(active->tls_credentials.identity, candidate->tls_credentials.identity, sizeof(active->tls_credentials.identity)) ||
         memcmp(active->tls_credentials.secret, candidate->tls_credentials.secret, 32U))) return 0;
    return ABoxMqttTrial_Start(trial, active, candidate, proof, now, timeout, restore_timeout);
}
int ABoxMaintenanceService_Poll(ABoxMqttTrial *trial,
    ABoxMqttTrialExecutor *executor, const ABoxMaintenanceServicePort *port,
    uint32_t now)
{
    ABoxMqttTrialExecutorAction action;
    uint64_t session;
    const ABoxMqttConfig *config;
    if (!trial || !executor || executor->trial != trial || !port || !port->action) return 0;
    ABoxMqttTrial_Poll(trial, now);
    if (!ABoxMqttTrialExecutor_Take(executor, &action, &session, &config)) return 0;
    if (!port->action(port->context, action, session, config, now))
        ABoxMqttTrial_Event(trial, session, ABOX_MQTT_TRIAL_ERROR, 0U, now);
    return 1;
}
ABoxConfigStoreResult ABoxMaintenanceService_Commit(ABoxMqttTrial *trial,
    uint64_t session, const ABoxConfigStorePort *store,
    const void *candidate, const void *stable, uint32_t now)
{
    ABoxConfigStoreResult result;
    if (!trial || trial->session != session || trial->state != ABOX_MQTT_TRIAL_COMMITTING)
        return ABOX_CONFIG_STORE_RECOVERY_FAILED;
    /* Expiry may enter RESTORING and must prevent any candidate Flash write. */
    ABoxMqttTrial_Poll(trial, now);
    if (trial->state != ABOX_MQTT_TRIAL_COMMITTING) return ABOX_CONFIG_STORE_RECOVERED;
    result = ABoxConfigStore_Commit(store, candidate, stable);
    ABoxMqttTrial_Event(trial, session,
        result == ABOX_CONFIG_STORE_SAVED ? ABOX_MQTT_TRIAL_SAVED : ABOX_MQTT_TRIAL_ERROR,
        0U, now);
    return result;
}
int ABoxMaintenanceService_Busy(const ABoxMqttTrial *trial)
{
    return trial && trial->state != ABOX_MQTT_TRIAL_IDLE &&
        trial->state != ABOX_MQTT_TRIAL_COMMITTED && trial->state != ABOX_MQTT_TRIAL_FAILED;
}
int ABoxMaintenanceService_RuntimeEvent(ABoxMqttTrial *trial,
    ABoxMqttTrialExecutor *executor, ABoxMqttRuntimeState state, uint32_t now)
{
    ABoxMqttTrialEvent event;
    ABoxMqttTrialExecutorAction expected, action;
    ABoxMqttTrialState expected_state;
    uint64_t session;
    const ABoxMqttConfig *config;
    if (!trial || !executor || executor->trial != trial) return -1;
    switch (state) {
    case ABOX_MQTT_RUNTIME_PREPARED:
        event = ABOX_MQTT_TRIAL_PREPARED; expected = ABOX_MQTT_TRIAL_ACTION_CONNECT;
        expected_state = ABOX_MQTT_TRIAL_CONNECTING; break;
    case ABOX_MQTT_RUNTIME_CONNECTED:
        event = ABOX_MQTT_TRIAL_CONNECTED; expected = ABOX_MQTT_TRIAL_ACTION_SUBSCRIBE;
        expected_state = ABOX_MQTT_TRIAL_SUBSCRIBING; break;
    case ABOX_MQTT_RUNTIME_READY:
        event = ABOX_MQTT_TRIAL_SUBSCRIBED; expected = ABOX_MQTT_TRIAL_ACTION_VERIFY;
        expected_state = ABOX_MQTT_TRIAL_VERIFYING; break;
    case ABOX_MQTT_RUNTIME_FAILED:
        ABoxMqttTrial_Event(trial, trial->session, ABOX_MQTT_TRIAL_ERROR, 0U, now);
        return ABOX_MQTT_TRIAL_ERROR;
    default: return -1;
    }
    ABoxMqttTrial_Event(trial, trial->session, event, 0U, now);
    if (trial->state == expected_state && ABoxMqttTrialExecutor_HasPending(executor)) {
        if (!ABoxMqttTrialExecutor_Take(executor, &action, &session, &config) ||
            action != expected || session != trial->session)
            ABoxMqttTrial_Event(trial, trial->session, ABOX_MQTT_TRIAL_ERROR, 0U, now);
    }
    return event;
}
