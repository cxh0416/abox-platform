#include "abox_cloud_response.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    char output[320];
    static const uint16_t codes[] = {200U, 202U, 400U, 404U, 409U, 413U, 500U, 503U};
    static const char *results[] = {NULL, "null", "{}", "{\"age\":0}", "\"日志\\n\""};
    for (unsigned c = 0; c < sizeof(codes)/sizeof(codes[0]); ++c)
        for (unsigned r = 0; r < sizeof(results)/sizeof(results[0]); ++r) {
            assert(ABoxCloudResponse_Encode(output, sizeof(output), "2.0", "1800000000001",
                NULL, "r\"\\\n", codes[c], "信息\n", results[r]));
            printf("wire:%s\n", output);
        }
    for (size_t capacity = 1U; capacity < sizeof(output); ++capacity) {
        int ok = ABoxCloudResponse_Encode(output, capacity, "2.0", "0", NULL,
            "r", 200U, "OK", "{}");
        if (!ok) assert(!output[0]);
        printf("capacity:%lu:%d:%s\n", (unsigned long)capacity, ok, output);
    }
    return 0;
}
