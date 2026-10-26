#include "abox_mqtt_v4.h"

#include <string.h>

int ABoxMqttV4_FormatInteger(char *output, size_t capacity, uint64_t value)
{
    char reversed[20];
    size_t digits = 0, i;
    if (!output || !capacity) return 0;
    output[0] = '\0';
    do { reversed[digits++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    if (digits >= capacity) return 0;
    for (i = 0; i < digits; ++i) output[i] = reversed[digits - i - 1U];
    output[digits] = '\0';
    return 1;
}

int ABoxMqttV4_RequestLifetimeValid(uint64_t timestamp, uint64_t expires,
                                     uint32_t maximum)
{
    return timestamp <= 9007199254740991ULL && expires <= 9007199254740991ULL &&
        expires > timestamp && (!maximum || expires - timestamp <= maximum);
}

uint16_t ABoxMqttV4_CheckExpiry(const ABoxClockSnapshot *clock, uint64_t expires)
{
    if (!clock || clock->quality != ABOX_CLOCK_SYNCHRONIZED) return 4003U;
    if (clock->utc_ms >= expires) return 4005U;
    if (expires - clock->utc_ms <= clock->error_ms) return 4003U;
    return 200U;
}

int ABoxMqttV4_DeviceIdValid(const char *id)
{
    size_t i;
    if (!id || !*id) return 0;
    for (i = 0; id[i]; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (i == 32U || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

int ABoxMqttV4_BuildTopic(char *output, size_t capacity, const char *device_id,
                          ABoxMqttV4Topic topic)
{
    static const char *const suffixes[ABOX_MQTT_V4_TOPIC_COUNT] = {
        "request", "response", "heartbeat", "manifest", "report"
    };
    static const char prefix[] = "/zxwl/abox/";
    const char *suffix;
    size_t prefix_len, id_len, suffix_len;
    if (!output || !capacity) return 0;
    output[0] = '\0';
    if (!ABoxMqttV4_DeviceIdValid(device_id) || (unsigned)topic >= ABOX_MQTT_V4_TOPIC_COUNT)
        return 0;
    suffix = suffixes[topic];
    prefix_len = sizeof(prefix) - 1U;
    id_len = strlen(device_id);
    suffix_len = strlen(suffix);
    if (id_len > capacity ||
        prefix_len + 1U + suffix_len >= capacity - id_len) return 0;
    memcpy(output, prefix, prefix_len);
    memcpy(output + prefix_len, device_id, id_len);
    output[prefix_len + id_len] = '/';
    memcpy(output + prefix_len + id_len + 1U, suffix, suffix_len + 1U);
    return 1;
}

int ABoxMqttV4_RegistryValid(const ABoxMqttV4Registry *registry)
{
    size_t i, j, k;
    if (!registry || (registry->count && !registry->profiles)) return 0;
    for (i = 0; i < registry->count; ++i) {
        const ABoxMqttProfileDescriptor *profile = registry->profiles[i];
        if (!profile || !profile->name || !profile->name[0] ||
            !profile->version || !profile->version[0] ||
            (profile->command_count && !profile->commands) ||
            (profile->report_count && !profile->reports)) return 0;
        for (j = 0; j < i; ++j)
            if (strcmp(profile->name, registry->profiles[j]->name) == 0) return 0;
        for (j = 0; j < profile->command_count; ++j) {
            const ABoxMqttCommandDescriptor *command = &profile->commands[j];
            if (!command->name || !command->name[0] || !command->profile ||
                strcmp(command->profile, profile->name) != 0 ||
                command->classification > ABOX_MQTT_COMMAND_PARAMETERIZED) return 0;
            for (k = 0; k < j; ++k)
                if (strcmp(command->name, profile->commands[k].name) == 0) return 0;
        }
        for (j = 0; j < profile->report_count; ++j) {
            const ABoxMqttReportDescriptor *report = &profile->reports[j];
            if (!report->name || !report->name[0] || !report->profile ||
                strcmp(report->profile, profile->name) != 0 ||
                !report->report_type || !report->report_type[0]) return 0;
            for (k = 0; k < j; ++k)
                if (strcmp(report->name, profile->reports[k].name) == 0) return 0;
        }
    }
    return 1;
}

const ABoxMqttProfileDescriptor *ABoxMqttV4_FindProfile(
    const ABoxMqttV4Registry *registry, const char *name)
{
    size_t i;
    if (!registry || !registry->profiles || !name) return 0;
    for (i = 0; i < registry->count; ++i) {
        const ABoxMqttProfileDescriptor *profile = registry->profiles[i];
        if (profile && profile->name && strcmp(profile->name, name) == 0) return profile;
    }
    return 0;
}

const ABoxMqttCommandDescriptor *ABoxMqttV4_FindCommand(
    const ABoxMqttProfileDescriptor *profile, const char *name)
{
    size_t i;
    if (!profile || !name) return 0;
    for (i = 0; i < profile->command_count; ++i)
        if (strcmp(profile->commands[i].name, name) == 0) return &profile->commands[i];
    return 0;
}

const ABoxMqttReportDescriptor *ABoxMqttV4_FindReport(
    const ABoxMqttProfileDescriptor *profile, const char *name)
{
    size_t i;
    if (!profile || !name) return 0;
    for (i = 0; i < profile->report_count; ++i)
        if (strcmp(profile->reports[i].name, name) == 0) return &profile->reports[i];
    return 0;
}

uint8_t ABoxMqttV4_SubscriptionQos(const char *topic)
{
    (void)topic;
    return 1U;
}

uint8_t ABoxMqttV4_ReportQos(const char *report_type)
{
    return (report_type && strcmp(report_type, "telemetry") == 0) ? 0U : 1U;
}

uint64_t ABoxMqttV4_IdempotencyProtectUntil(uint64_t expires_at)
{
    if (UINT64_MAX - expires_at < ABOX_MQTT_V4_IDEMPOTENCY_GRACE_MS) return UINT64_MAX;
    return expires_at + ABOX_MQTT_V4_IDEMPOTENCY_GRACE_MS;
}
