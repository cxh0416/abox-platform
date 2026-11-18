#include "abox_cloud_response.h"
#include "abox_cloud_v4.h"
#include "abox_mqtt_digest.h"
#include "abox_mqtt_v4.h"
#include <assert.h>
#include <string.h>

/* Compare every byte of the streaming canonical request, including its end. */
static int expect_bytes(void *context, const char *bytes, size_t count)
{
    const char **expected = context;
    if (count > strlen(*expected) || memcmp(*expected, bytes, count)) return 0;
    *expected += count;
    return 1;
}

static int null_params(void *value, ABoxCanonicalWriter write, void *context)
{
    (void)value;
    return write(context, "null", 4);
}

int main(void)
{
    char number[21], packet[192];
    const uint64_t max_epoch = 9007199254740991ULL; /* V4 maximum-safe-epoch */
    const char *expected = "{\"command\":\"test\",\"expiresAt\":9007199254740991,\"params\":null}";
    assert(ABoxMqttV4_FormatInteger(number, sizeof(number), 0) && !strcmp(number, "0"));
    assert(ABoxMqttV4_FormatInteger(number, sizeof(number), UINT64_MAX));
    assert(!strcmp(number, "18446744073709551615"));
    assert(!ABoxMqttV4_FormatInteger(number, sizeof(number) - 1U, UINT64_MAX) && !number[0]);
    assert(ABoxCloudResponse_EncodeU64(packet, sizeof(packet), "2.0", 1731912345678ULL,
        "cage_dumper", "info-1", 200, "ok", "{}"));
    assert(!strcmp(packet, "{\"version\":\"2.0\",\"timestamp\":1731912345678,\"data\":{"
        "\"product\":\"cage_dumper\",\"requestId\":\"info-1\",\"code\":200,\"msg\":\"ok\",\"result\":{}}}"));
    assert(ABoxCloudV4_Heartbeat(packet, sizeof(packet), max_epoch, "test", NULL));
    assert(!strcmp(packet, "{\"version\":\"4.0\",\"timestamp\":9007199254740991,\"data\":{\"firmware\":\"test\"}}"));
    assert(!ABoxCloudV4_Heartbeat(packet, sizeof(packet), max_epoch + 1U, "test", NULL));
    assert(ABoxMqttDigest_WriteRequest("test", 1, max_epoch, null_params, NULL, expect_bytes, &expected));
    assert(!*expected);
    expected = "{\"command\":\"test\",\"expiresAt\":9007199254740992,\"params\":null}";
    assert(!ABoxMqttDigest_WriteRequest("test", 1, max_epoch + 1U, null_params, NULL, expect_bytes, &expected));
    return 0;
}
