#include "abox_mqtt_request.h"
#include <string.h>

static int alnum(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
int ABoxMqttV4_IdValid(const char *s, ABoxMqttIdKind kind)
{
    size_t i, limit;
    if (!s || !*s || (unsigned)kind > ABOX_MQTT_ID_INSTANCE) return 0;
    if (kind == ABOX_MQTT_ID_DEVICE) return ABoxMqttV4_DeviceIdValid(s);
    limit = kind == ABOX_MQTT_ID_REPORT ? 96U :
        (kind == ABOX_MQTT_ID_DEVICE || kind == ABOX_MQTT_ID_INSTANCE ? 32U : 64U);
    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (i == limit) return 0;
        if (alnum(c)) continue;
        if (c == '_' || c == '-') {
            if (i || kind == ABOX_MQTT_ID_DEVICE || kind == ABOX_MQTT_ID_INSTANCE) continue;
        } else if (i && kind != ABOX_MQTT_ID_DEVICE && kind != ABOX_MQTT_ID_INSTANCE &&
                   (c == '.' || c == ':')) continue;
        return 0;
    }
    return 1;
}
int ABoxMqttV4_NameValid(const char *s)
{
    size_t i; unsigned segments = 1;
    if (!s || *s < 'a' || *s > 'z') return 0;
    for (i = 0; s[i]; ++i) {
        char c = s[i];
        if (i == 64U) return 0;
        if (c == '_') {
            if (!i || s[i-1] == '_' || !s[i+1] || ++segments > 8U) return 0;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}
int ABoxMqttV4_ProfileVersionValid(const char *s)
{
    unsigned part;
    if (!s) return 0;
    for (part = 0; part < 2U; ++part) {
        unsigned value = 0, digits = 0;
        const char *first = s;
        while (*s >= '0' && *s <= '9') {
            if (++digits > 5U) return 0;
            value = value * 10U + (unsigned)(*s++ - '0');
        }
        if (!digits || value > 65535U || (digits > 1U && *first == '0')) return 0;
        if (part == 0U) { if (*s++ != '.') return 0; }
        else if (*s) return 0;
    }
    return 1;
}
static int integer(const cJSON *n, uint64_t *value)
{
    if (!cJSON_IsNumber(n) || n->valuedouble < 0 || n->valuedouble > 9007199254740991.0 ||
        n->valuedouble != (double)(uint64_t)n->valuedouble) return 0;
    *value = (uint64_t)n->valuedouble; return 1;
}
static int count_bytes(void *context, const char *bytes, size_t length)
{
    size_t *size = context; (void)bytes;
    if (length > ABOX_MQTT_V4_VALUE_LIMIT - *size) return 0;
    *size += length; return 1;
}
void ABoxMqttRequest_Close(ABoxMqttRequest *r)
{
    if (r) { cJSON_Delete(r->root); memset(r, 0, sizeof(*r)); }
}
int ABoxMqttRequest_Normalize(ABoxMqttRequest *r, ABoxCanonicalEncoder params, void *context)
{
    if (!r || !r->command || !params) return 0;
    return ABoxMqttDigest_RequestStream(r->command,r->has_expires_at,r->expires_at,params,context,r->digest);
}
uint16_t ABoxMqttRequest_Decode(ABoxMqttRequest *r, const char *text, size_t length,
    ABoxMqttNumberField number_field, void *context)
{
    const cJSON *data, *id, *command, *version, *expires;
    size_t size = 0;
    if (!r) return 4001U;
    memset(r, 0, sizeof(*r));
    if (!text || !length || length > ABOX_MQTT_V4_PACKET_LIMIT) return 4001U;
    r->root = ABoxMqttDigest_ParseProfile(text, length, number_field, context);
    if (!cJSON_IsObject(r->root)) return 4001U;
    data = cJSON_GetObjectItemCaseSensitive(r->root, "data");
    if (!cJSON_IsObject(data)) return 4001U;
    id = cJSON_GetObjectItemCaseSensitive(data, "requestId");
    if (!cJSON_IsString(id) || !ABoxMqttV4_IdValid(id->valuestring, ABOX_MQTT_ID_REQUEST)) return 4001U;
    r->request_id = id->valuestring;
    command = cJSON_GetObjectItemCaseSensitive(data, "command");
    version = cJSON_GetObjectItemCaseSensitive(r->root, "version");
    r->params = cJSON_GetObjectItemCaseSensitive(data, "params");
    expires = cJSON_GetObjectItemCaseSensitive(data, "expiresAt");
    if (!cJSON_IsString(version) || strcmp(version->valuestring, ABOX_MQTT_V4_VERSION) ||
        !integer(cJSON_GetObjectItemCaseSensitive(r->root, "timestamp"), &r->timestamp) ||
        !cJSON_IsString(command) || !ABoxMqttV4_NameValid(command->valuestring) ||
        (!cJSON_IsObject(r->params) && !cJSON_IsNull(r->params)) ||
        (expires && !integer(expires, &r->expires_at)) ||
        !ABoxMqttDigest_Canonical(r->params, count_bytes, &size)) return 4001U;
    r->command = command->valuestring; r->has_expires_at = expires != NULL;
    return ABoxMqttDigest_Request(data, NULL, r->digest) ? 200U : 4001U;
}
uint16_t ABoxMqttRequest_Resolve(const ABoxMqttRequest *r,
    const ABoxMqttOperation *ops, size_t count, const ABoxMqttOperation **result)
{
    static const ABoxMqttOperation sync = {"sync_state", NULL, ABOX_MQTT_COMMAND_QUERY,
        ABOX_MQTT_EXPIRY_FORBIDDEN, 0, NULL, NULL, NULL};
    size_t i, matches = 0; int known = 0;
    const ABoxMqttOperation *op = NULL;
    if (result) *result = NULL;
    if (!r || !r->command || !result || (count && !ops)) return 4001U;
    if (!strcmp(r->command, "sync_state")) {
        if (!cJSON_IsNull(r->params) || r->has_expires_at) return 4001U;
        *result = &sync; return 200U;
    }
    for (i = 0; i < count; ++i) {
        if (!ops[i].command || strcmp(ops[i].command, r->command)) continue;
        known = 1;
        if (!ops[i].matches || ops[i].matches(ops[i].context, r->params)) { op = &ops[i]; ++matches; }
    }
    if (!known) return 4002U;
    if (matches != 1U || !op->profile || !ABoxMqttV4_NameValid(op->profile) ||
        op->classification < ABOX_MQTT_COMMAND_QUERY || op->classification > ABOX_MQTT_COMMAND_SAFETY_STOP ||
        (unsigned)op->expiry > ABOX_MQTT_EXPIRY_REQUIRED ||
        (op->classification == ABOX_MQTT_COMMAND_DISCRETE_ACTION &&
         (op->expiry != ABOX_MQTT_EXPIRY_REQUIRED || !op->max_validity_ms || op->max_validity_ms > 30000U))) return 4001U;
    if (op->validate) {
        uint16_t code = op->validate(op->context, r->params);
        if (code != 200U) return code;
    }
    if ((op->expiry == ABOX_MQTT_EXPIRY_REQUIRED && !r->has_expires_at) ||
        (op->expiry == ABOX_MQTT_EXPIRY_FORBIDDEN && r->has_expires_at) ||
        (r->has_expires_at && !ABoxMqttV4_RequestLifetimeValid(r->timestamp, r->expires_at, op->max_validity_ms))) return 4001U;
    *result = op; return 200U;
}
uint16_t ABoxMqttRequest_CheckNew(const ABoxMqttRequest *r,
    const ABoxMqttOperation *op, const ABoxClockSnapshot *clock)
{
    if (!r || !op) return 4001U;
    return r->has_expires_at ? ABoxMqttV4_CheckExpiry(clock, r->expires_at) : 200U;
}
