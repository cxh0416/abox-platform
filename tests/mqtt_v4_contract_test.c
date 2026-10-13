#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "abox_mqtt_v4.h"

int main(void)
{
    static const char *const suffixes[ABOX_MQTT_V4_TOPIC_COUNT] = {
        "request", "response", "heartbeat", "manifest", "report"
    };
    static const ABoxMqttCommandDescriptor commands[] = {
        {"query", "fixture", ABOX_MQTT_COMMAND_QUERY}
    };
    static const ABoxMqttReportDescriptor reports[] = {
        {"status", "fixture", "state"}
    };
    static const ABoxMqttProfileDescriptor profile = {
        "fixture", "1.0", commands, 1U, reports, 1U
    };
    const ABoxMqttProfileDescriptor *profiles[] = {&profile};
    const ABoxMqttV4Registry registry = {profiles, 1U};
    char topic[80];
    char expected[80];
    size_t i;

    for (i = 0; i < ABOX_MQTT_V4_TOPIC_COUNT; ++i) {
        assert(ABoxMqttV4_BuildTopic(topic, sizeof(topic), "device-1",
                                     (ABoxMqttV4Topic)i));
        strcpy(expected, "/zxwl/abox/device-1/");
        strcat(expected, suffixes[i]);
        assert(strcmp(topic, expected) == 0);
    }
    assert(!ABoxMqttV4_BuildTopic(topic, 8U, "device-1", ABOX_MQTT_V4_TOPIC_REPORT));
    assert(topic[0] == '\0');
    assert(!ABoxMqttV4_BuildTopic(topic, sizeof(topic), "", ABOX_MQTT_V4_TOPIC_REPORT));
    assert(!ABoxMqttV4_BuildTopic(topic, sizeof(topic), "device-1", ABOX_MQTT_V4_TOPIC_COUNT));
    char full_device_id[33];
    memset(full_device_id, 'a', 32); full_device_id[32] = '\0';
    assert(ABoxMqttV4_BuildTopic(topic, sizeof(topic), full_device_id, ABOX_MQTT_V4_TOPIC_REPORT));
    assert(strstr(topic, full_device_id)); /* Provisioning capacity is not the wire ceiling. */

    assert(ABoxMqttV4_RegistryValid(&registry));
    assert(ABoxMqttV4_FindProfile(&registry, "fixture") == &profile);
    assert(ABoxMqttV4_FindProfile(&registry, "missing") == 0);
    assert(ABoxMqttV4_FindCommand(&profile, "query") == commands);
    assert(ABoxMqttV4_FindReport(&profile, "status") == reports);
    assert(ABoxMqttV4_SubscriptionQos("/zxwl/abox/device/request") == 1U);
    assert(ABoxMqttV4_ReportQos("state") == 1U);
    assert(ABoxMqttV4_ReportQos("event") == 1U);
    assert(ABoxMqttV4_ReportQos("telemetry") == 0U);
    assert(ABoxMqttV4_IdempotencyProtectUntil(1770186098000ULL) == 1770186698000ULL);
    assert(ABoxMqttV4_IdempotencyProtectUntil(UINT64_MAX) == UINT64_MAX);
    return 0;
}
