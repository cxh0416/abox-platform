#include <assert.h>
#include <string.h>

#include "abox_cloud_internal_v2.h"

static int open(const char *json, const char *product,
                ABoxCloudInternalV2Envelope *envelope)
{
    return ABoxCloudInternalV2_Open(json, strlen(json), product, 64U, envelope);
}

int main(void)
{
    ABoxCloudInternalV2Envelope envelope;
    char id[64];
    const char *valid = "{\"version\":\"2.0\",\"data\":{\"product\":\"patrol_vehicle_chassis\",\"requestId\":\"abc-1\",\"command\":\"internal_config\",\"params\":{\"action\":\"get_info\"}}}";
    const char *wrong_product = "{\"version\":\"2.0\",\"data\":{\"product\":\"other\",\"requestId\":\"abc-1\",\"command\":\"internal_config\",\"params\":{}}}";
    const char *missing_params = "{\"version\":\"2.0\",\"data\":{\"requestId\":\"abc-1\",\"command\":\"internal_config\"}}";
    assert(ABoxCloudRequestId_Valid("abc-1", sizeof(id)));
    assert(!ABoxCloudRequestId_Valid("a b", sizeof(id)));
    assert(!ABoxCloudRequestId_Valid("", sizeof(id)));
    assert(!ABoxCloudRequestId_Valid("abc", 3U));
    ABoxCloudRequestId_ExtractRaw("bad {\"requestId\" : \"abc-1\"",
                                   strlen("bad {\"requestId\" : \"abc-1\""), id, sizeof(id));
    assert(strcmp(id, "abc-1") == 0);
    ABoxCloudRequestId_ExtractRaw("{\"requestId\":\"a\\\"b\"}",
                                   strlen("{\"requestId\":\"a\\\"b\"}"), id, sizeof(id));
    assert(id[0] == '\0');

    assert(open(valid, "patrol_vehicle_chassis", &envelope));
    assert(strcmp(envelope.request_id, "abc-1") == 0);
    assert(strcmp(envelope.command->valuestring, "internal_config") == 0);
    assert(cJSON_IsObject(envelope.params));
    ABoxCloudInternalV2_Close(&envelope);
    assert(envelope.root == 0);
    assert(!open(wrong_product, "patrol_vehicle_chassis", &envelope));
    assert(open(wrong_product, 0, &envelope));
    ABoxCloudInternalV2_Close(&envelope);
    assert(!open(missing_params, 0, &envelope));
    assert(!open("{\"version\":\"4.0\",\"data\":{}}", 0, &envelope));
    assert(!open("{\"version\":\"2.0\",\"data\":{\"requestId\":\"bad id\",\"command\":\"x\",\"params\":{}}}", 0, &envelope));
    {
        ABoxCloudInternalV2Fields fields;
        cJSON *root = cJSON_Parse("{\"VERSION\":\"2.0\",\"timestamp\":-1.5,\"DATA\":{\"requestId\":\"\",\"COMMAND\":\"internal_config\",\"params\":null}}");
        assert(ABoxCloudInternalV2_Read(root, 0, 65U, ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT,
            &fields) == ABOX_INTERNAL_V2_OK);
        assert(fields.request_id[0] == 0 && cJSON_IsNull(fields.params));
        assert(ABoxCloudInternalV2_Read(root, 0, 64U, ABOX_INTERNAL_V2_STRICT,
            &fields) == ABOX_INTERNAL_V2_INVALID_DATA);
        cJSON_Delete(root);
        root = cJSON_Parse("{\"version\":\"2.0\",\"data\":{\"requestId\":\"r\",\"command\":\"x\",\"params\":{}}}");
        assert(ABoxCloudInternalV2_Read(root, 0, 65U, ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT,
            &fields) == ABOX_INTERNAL_V2_INVALID_VERSION_TIMESTAMP);
        assert(strcmp(fields.request_id, "r") == 0);
        assert(ABoxCloudInternalV2_Read(root, 0, 64U, ABOX_INTERNAL_V2_STRICT,
            &fields) == ABOX_INTERNAL_V2_OK);
        cJSON_Delete(root);
        root = cJSON_Parse("{\"version\":\"2.0\",\"timestamp\":0,\"data\":{\"requestId\":\"r\",\"params\":[]}}");
        assert(ABoxCloudInternalV2_Read(root, 0, 65U, ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT,
            &fields) == ABOX_INTERNAL_V2_INVALID_COMMAND);
        cJSON_Delete(root);
        root = cJSON_Parse("{\"version\":\"2.0\",\"timestamp\":0,\"data\":{\"requestId\":\"a b\",\"command\":\"x\",\"params\":[]}}");
        assert(ABoxCloudInternalV2_Read(root, 0, 65U, ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT,
            &fields) == ABOX_INTERNAL_V2_OK);
        assert(ABoxCloudInternalV2_Read(root, 0, 64U, ABOX_INTERNAL_V2_STRICT,
            &fields) == ABOX_INTERNAL_V2_INVALID_ID);
        cJSON_Delete(root);
    }
    return 0;
}
