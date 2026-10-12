#include "abox_cloud_internal_v2.h"

#include <string.h>

int ABoxCloudRequestId_Valid(const char *value, size_t capacity)
{
    size_t i;
    if (!value || !capacity || value[0] == '\0') return 0;
    for (i = 0U; i < capacity; ++i) {
        uint8_t ch = (uint8_t)value[i];
        if (ch == '\0') return 1;
        if (ch < 0x21U || ch > 0x7EU) return 0;
    }
    return 0;
}

void ABoxCloudRequestId_ExtractRaw(const char *payload, size_t length,
                                   char *output, size_t capacity)
{
    static const char key[] = "\"requestId\"";
    size_t i;
    if (!output || !capacity) return;
    output[0] = '\0';
    if (!payload || length < sizeof(key)) return;
    for (i = 0U; i + sizeof(key) - 1U < length; ++i) {
        size_t pos, used = 0U;
        if (memcmp(payload + i, key, sizeof(key) - 1U) != 0) continue;
        pos = i + sizeof(key) - 1U;
        while (pos < length && (payload[pos] == ' ' || payload[pos] == '\t' ||
               payload[pos] == '\r' || payload[pos] == '\n' || payload[pos] == ':')) ++pos;
        if (pos >= length || payload[pos++] != '"') return;
        while (pos < length && payload[pos] != '"' && used < capacity - 1U) {
            if (payload[pos] == '\\') {
                output[0] = '\0';
                return;
            }
            output[used++] = payload[pos++];
        }
        output[used] = '\0';
        if (!ABoxCloudRequestId_Valid(output, capacity)) output[0] = '\0';
        return;
    }
}

ABoxCloudInternalV2Status ABoxCloudInternalV2_Read(cJSON *root,
    const char *expected_product, size_t request_id_capacity,
    ABoxCloudInternalV2Policy policy, ABoxCloudInternalV2Fields *fields)
{
    cJSON *version, *timestamp, *data, *product, *id;
    cJSON *(*item)(const cJSON *, const char *) =
        policy == ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT ?
        cJSON_GetObjectItem : cJSON_GetObjectItemCaseSensitive;
    if (!fields) return ABOX_INTERNAL_V2_INVALID_DATA;
    memset(fields, 0, sizeof(*fields));
    data = item(root, "data");
    if (!cJSON_IsObject(data)) return ABOX_INTERNAL_V2_INVALID_DATA;
    id = item(data, "requestId");
    if (!request_id_capacity || !cJSON_IsString(id) || !id->valuestring ||
        (policy == ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT ?
         strlen(id->valuestring) >= request_id_capacity :
         !ABoxCloudRequestId_Valid(id->valuestring, request_id_capacity)))
        return ABOX_INTERNAL_V2_INVALID_ID;
    fields->request_id = id->valuestring;
    version = item(root, "version");
    timestamp = item(root, "timestamp");
    if (!cJSON_IsString(version) || strcmp(version->valuestring, "2.0") ||
        (policy == ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT && !cJSON_IsNumber(timestamp)))
        return ABOX_INTERNAL_V2_INVALID_VERSION_TIMESTAMP;
    fields->command = item(data, "command");
    fields->params = item(data, "params");
    if (!cJSON_IsString(fields->command)) return ABOX_INTERNAL_V2_INVALID_COMMAND;
    if (policy == ABOX_INTERNAL_V2_STRICT && !cJSON_IsObject(fields->params))
        return ABOX_INTERNAL_V2_INVALID_PARAMS;
    product = item(data, "product");
    if (expected_product && product &&
        (!cJSON_IsString(product) || strcmp(product->valuestring, expected_product)))
        return ABOX_INTERNAL_V2_INVALID_PRODUCT;
    return ABOX_INTERNAL_V2_OK;
}

int ABoxCloudInternalV2_Open(const char *payload, size_t length,
                             const char *expected_product,
                             size_t request_id_capacity,
                             ABoxCloudInternalV2Envelope *envelope)
{
    ABoxCloudInternalV2Fields fields;
    if (!envelope) return 0;
    memset(envelope, 0, sizeof(*envelope));
    if (!payload || !request_id_capacity) return 0;
    envelope->root = cJSON_ParseWithLength(payload, length);
    if (!envelope->root) return 0;
    if (ABoxCloudInternalV2_Read(envelope->root, expected_product, request_id_capacity,
            ABOX_INTERNAL_V2_STRICT, &fields) != ABOX_INTERNAL_V2_OK) {
        ABoxCloudInternalV2_Close(envelope);
        return 0;
    }
    envelope->params = fields.params;
    envelope->request_id = fields.request_id;
    envelope->command = fields.command;
    return 1;
}

void ABoxCloudInternalV2_Close(ABoxCloudInternalV2Envelope *envelope)
{
    if (!envelope) return;
    cJSON_Delete(envelope->root);
    memset(envelope, 0, sizeof(*envelope));
}

int ABoxCloudInternalV2_PskCredentials(const cJSON *params, ABoxTlsCredentials *output)
{
    const cJSON *identity = cJSON_GetObjectItemCaseSensitive(params, "identity");
    const cJSON *secret = cJSON_GetObjectItemCaseSensitive(params, "secret");
    const cJSON *generation = cJSON_GetObjectItemCaseSensitive(params, "generation");
    memset(output, 0, sizeof(*output));
    if (!cJSON_IsString(identity) || strlen(identity->valuestring) >= sizeof(output->identity) ||
        !cJSON_IsString(secret) || strlen(secret->valuestring) != 32U ||
        !cJSON_IsNumber(generation) || generation->valuedouble < 1 ||
        generation->valuedouble > 4294967295.0 ||
        generation->valuedouble != (uint32_t)generation->valuedouble) return 0;
    output->mode = ABOX_TLS_AUTH_PSK;
    output->generation = (uint32_t)generation->valuedouble;
    strcpy(output->identity, identity->valuestring);
    strcpy(output->secret, secret->valuestring);
    if (ABoxTlsCredentials_Valid(output)) return 1;
    memset(output, 0, sizeof(*output));
    return 0;
}
