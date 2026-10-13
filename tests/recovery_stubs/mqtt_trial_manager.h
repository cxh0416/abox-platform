#include "abox_mqtt_trial.h"
ABoxMqttTrialState MqttTrialManager_State(void);
uint8_t MqttTrialManager_RuntimeRecoveryAllowed(void);
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState, uint32_t);
