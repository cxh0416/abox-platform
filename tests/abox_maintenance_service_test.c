#include "abox_maintenance_service.h"
#include <assert.h>
#include <string.h>
static ABoxMqttTrialExecutorAction last;
static int accept = 1, writes, fail_write;
static int action(void *context, ABoxMqttTrialExecutorAction value,
    uint64_t session, const ABoxMqttConfig *config, uint32_t now)
{ (void)context; (void)session; (void)config; (void)now; last = value; return accept; }
static int write_snapshot(void *context, const void *snapshot)
{ (void)context; (void)snapshot; ++writes; return writes != fail_write; }
static void verify(ABoxMqttTrial *trial, ABoxMqttTrialExecutor *executor,
    ABoxMaintenanceServicePort *port, const ABoxMqttConfig *config)
{
    assert(ABoxMqttTrialExecutor_Init(executor, trial));
    assert(ABoxMqttTrial_StartFirst(trial, config, 1U, 0U));
    assert(ABoxMaintenanceService_Poll(trial, executor, port, 1U));
    assert(ABoxMaintenanceService_RuntimeEvent(trial, executor, ABOX_MQTT_RUNTIME_PREPARED, 2U) == ABOX_MQTT_TRIAL_PREPARED);
    assert(!ABoxMqttTrialExecutor_HasPending(executor));
    (void)ABoxMaintenanceService_RuntimeEvent(trial, executor, ABOX_MQTT_RUNTIME_CONNECTED, 3U);
    (void)ABoxMaintenanceService_RuntimeEvent(trial, executor, ABOX_MQTT_RUNTIME_READY, 4U);
    assert(trial->state == ABOX_MQTT_TRIAL_VERIFYING);
    assert(!ABoxMqttTrialExecutor_HasPending(executor));
    ABoxMqttTrial_Event(trial, trial->session, ABOX_MQTT_TRIAL_PROVED, 1U, 5U);
}
int main(void)
{
    ABoxMqttTrial trial;
    ABoxMqttTrialExecutor executor;
    ABoxMqttConfig config = {"broker", 1883U, "u", "p", 0, 0};
    ABoxMaintenanceServicePort port = {0, action};
    ABoxConfigStorePort store = {0, write_snapshot};
    {
        ABoxMqttConfig active = {"broker", 19688U, "u", "p", 1U, 0U};
        ABoxMqttConfig candidate = {"next", 19688U, "u", "p", 1U, 0U};
        assert(ABoxMqttTrialExecutor_Init(&executor, &trial));
        assert(!ABoxMaintenanceService_StartTls(&trial, &config, &candidate, 1U, 0U, 100U, 100U));
        assert(!ABoxMaintenanceService_StartTls(&trial, &active, &config, 1U, 0U, 100U, 100U));
        candidate.tls_credentials.mode = ABOX_TLS_AUTH_PSK;
        assert(!ABoxMaintenanceService_StartTls(&trial, &active, &candidate, 1U, 0U, 100U, 100U));
        candidate.tls_credentials.mode = ABOX_TLS_AUTH_CA;
        assert(ABoxMaintenanceService_StartTls(&trial, &active, &candidate, 1U, 0U, 100U, 100U));
        assert(ABoxMqttTrialExecutor_Init(&executor, &trial));
        active.tls_credentials.mode = ABOX_TLS_AUTH_PSK;
        active.tls_credentials.generation = 1U;
        strcpy(active.tls_credentials.identity, "device.1");
        strcpy(active.tls_credentials.secret, "01234567890123456789012345678901");
        candidate.tls_credentials = active.tls_credentials;
        candidate.tls_credentials.secret[0] = 'x';
        assert(!ABoxMaintenanceService_StartTls(&trial, &active, &candidate, 1U, 0U, 100U, 100U));
        candidate.tls_credentials = active.tls_credentials;
        assert(ABoxMaintenanceService_StartTls(&trial, &active, &candidate, 1U, 0U, 100U, 100U));
    }
    assert(ABoxMqttTrialExecutor_Init(&executor, &trial));
    assert(ABoxMqttTrial_StartFirst(&trial, &config, 1U, 0U));
    assert(ABoxMaintenanceService_Poll(&trial, &executor, &port, 1U));
    assert(last == ABOX_MQTT_TRIAL_ACTION_PREPARE);
    accept = 0;
    ABoxMqttTrial_Event(&trial, trial.session, ABOX_MQTT_TRIAL_PREPARED, 0U, 2U);
    assert(ABoxMaintenanceService_Poll(&trial, &executor, &port, 3U));
    assert(trial.state == ABOX_MQTT_TRIAL_RESTORING);
    assert(ABoxMaintenanceService_Poll(&trial, &executor, &port, 4U));
    assert(trial.state == ABOX_MQTT_TRIAL_RESTORE_FAILED);
    assert(ABoxMaintenanceService_Busy(&trial));
    assert(ABoxMaintenanceService_Commit(&trial, trial.session - 1U, &store,
        &config, &config, 5U) == ABOX_CONFIG_STORE_RECOVERY_FAILED);
    assert(!writes);
    accept = 1;
    verify(&trial, &executor, &port, &config);
    {
        int stable = 1, candidate = 2;
        assert(ABoxMaintenanceService_Commit(&trial, trial.session, &store,
            &candidate, &stable, 6U) == ABOX_CONFIG_STORE_SAVED);
        assert(trial.state == ABOX_MQTT_TRIAL_COMMITTED && writes == 1);
        writes = 0; fail_write = 1;
        verify(&trial, &executor, &port, &config);
        assert(ABoxMaintenanceService_Commit(&trial, trial.session, &store,
            &candidate, &stable, 6U) == ABOX_CONFIG_STORE_RECOVERED);
        assert(trial.state == ABOX_MQTT_TRIAL_RESTORING && writes == 2);
        assert(ABoxMqttTrialExecutor_HasPending(&executor));
    }
    return 0;
}
