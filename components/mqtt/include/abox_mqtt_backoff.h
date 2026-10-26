#ifndef ABOX_MQTT_BACKOFF_H
#define ABOX_MQTT_BACKOFF_H
#include <stdint.h>
typedef struct {
    uint32_t random, ready_since;
    uint8_t failures, online;
} ABoxMqttBackoff;
/* Seed from a device-specific value plus a changing boot source. */
void ABoxMqttBackoff_Init(ABoxMqttBackoff *backoff, uint32_t seed);
uint32_t ABoxMqttBackoff_Initial(ABoxMqttBackoff *backoff);
uint32_t ABoxMqttBackoff_Failed(ABoxMqttBackoff *backoff);
void ABoxMqttBackoff_Online(ABoxMqttBackoff *backoff, uint32_t now);
void ABoxMqttBackoff_Poll(ABoxMqttBackoff *backoff, uint32_t now);
#endif
