#include "abox_cloud_response.h"
#include <assert.h>
#include <string.h>
static int result(void *context, char *output, size_t capacity)
{
    const char *json = context;
    if (strlen(json) >= capacity) return 0;
    strcpy(output, json);
    return 1;
}
static int unterminated(void *context, char *output, size_t capacity)
{
    (void)context;
    memset(output, 'x', capacity);
    return 1;
}
int main(void)
{
    char output[256], tiny[12];
    assert(ABoxCloudResponse_Encode(output, sizeof(output), "2.0", "9007199254740993",
        "product", "r\"1", 200U, "ok\n", "{\"saved\":true}"));
    assert(strcmp(output, "{\"version\":\"2.0\",\"timestamp\":9007199254740993,\"data\":{\"product\":\"product\",\"requestId\":\"r\\\"1\",\"code\":200,\"msg\":\"ok\\n\",\"result\":{\"saved\":true}}}") == 0);
    assert(ABoxCloudResponse_Encode(output, sizeof(output), "2.0", "0", 0, "r", 500U, "error", 0));
    assert(!strstr(output, "product"));
    assert(!ABoxCloudResponse_Encode(tiny, sizeof(tiny), "2.0", "0", 0, "r", 200U, "ok", 0));
    assert(tiny[0] == 0);
    assert(!ABoxCloudResponse_Encode(output, sizeof(output), "2.0", "1e9", 0, "r", 200U, "ok", 0));
    assert(!ABoxCloudResponse_Encode(output, sizeof(output), "2.0", "", 0, "r", 200U, "ok", 0));
    assert(ABoxCloudResponse_EncodeResult(output, sizeof(output), "2.0", "1800000000000",
        0, "r\"\\\n", 202U, "已受理", result, "null"));
    assert(strcmp(output, "{\"version\":\"2.0\",\"timestamp\":1800000000000,\"data\":{\"requestId\":\"r\\\"\\\\\\n\",\"code\":202,\"msg\":\"已受理\",\"result\":null}}") == 0);
    assert(ABoxCloudResponse_EncodeResult(output, sizeof(output), "2.0", "0",
        0, "r", 500U, "response too large", 0, 0));
    assert(!strstr(output, "result"));
    assert(!ABoxCloudResponse_EncodeResult(tiny, sizeof(tiny), "2.0", "0",
        0, "r", 200U, "ok", result, "null") && !tiny[0]);
    assert(!ABoxCloudResponse_EncodeResult(output, sizeof(output), "2.0", "0",
        0, "r", 200U, "ok", unterminated, 0) && !output[0]);
    return 0;
}
