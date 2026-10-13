#include "abox_mqtt_digest.h"
#include "abox_sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct { char text[64]; size_t used; } Text;
static int capture(void *context, const char *s, size_t n)
{
    Text *out = context;
    if (n >= sizeof(out->text) - out->used) return 0;
    memcpy(out->text + out->used, s, n); out->used += n; out->text[out->used] = 0; return 1;
}
static int fail_params(void *value, ABoxCanonicalWriter write, void *context)
{ (void)value; (void)write; (void)context; return 0; }
static void check_stream(void)
{
    const int32_t values[] = {0, 1, -1, 10, -200, 12345, -30000, INT32_MIN, INT32_MAX};
    const char *texts[] = {"0", "0.001", "-0.001", "0.01", "-0.2", "12.345", "-30", "-2147483.648", "2147483.647"};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        Text out = {{0}, 0};
        assert(ABoxMqttDigest_WriteMilli(values[i], capture, &out));
        assert(!strcmp(out.text, texts[i]));
    }
    Text out = {{0}, 0};
    assert(!ABoxMqttDigest_WriteInteger(INT64_MIN, capture, &out) && !out.used);
    uint8_t digest[32], zero[32] = {0}; memset(digest, 0xaa, 32);
    assert(!ABoxMqttDigest_RequestStream("test", 0, 0, fail_params, NULL, digest));
    assert(!memcmp(digest, zero, 32));
    assert(!ABoxMqttDigest_NullRequest("power_on", 1, 9007199254740992ULL, digest));
    cJSON null = {0}, bad = {0}; null.type = cJSON_NULL; bad.type = cJSON_Number; bad.valuedouble = NAN;
    assert(!ABoxMqttDigest_RequestFields("power_on", &null, &bad, digest));
    assert(!memcmp(digest, zero, 32));
}

static int output(void *ctx, const char *s, size_t n)
{ (void)ctx; return fwrite(s, 1, n, stdout) == n; }
static void check_sha(void)
{
    ABoxSha256 ctx; uint8_t digest[32]; char hex[65];
    ABoxSha256_Init(&ctx); ABoxSha256_Final(&ctx, digest);
    for (unsigned i = 0; i < 32; ++i) sprintf(hex + i * 2, "%02x", digest[i]);
    assert(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    ABoxSha256_Init(&ctx);
    ABoxSha256_Update(&ctx, (const uint8_t *)"a", 1);
    ABoxSha256_Update(&ctx, (const uint8_t *)"bc", 2);
    ABoxSha256_Final(&ctx, digest);
    for (unsigned i = 0; i < 32; ++i) sprintf(hex + i * 2, "%02x", digest[i]);
    assert(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    char block[1000]; memset(block, 'a', sizeof(block)); ABoxSha256_Init(&ctx);
    for (unsigned i = 0; i < 1000; ++i) ABoxSha256_Update(&ctx, (const uint8_t *)block, sizeof(block));
    ABoxSha256_Final(&ctx, digest);
    for (unsigned i = 0; i < 32; ++i) sprintf(hex + i * 2, "%02x", digest[i]);
    assert(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
int main(int argc, char **argv)
{
    if (argc == 1) { check_sha(); check_stream(); return 0; }
    if (!strcmp(argv[1], "milli-range")) {
        for (int32_t milli = -30000; milli <= 30000; ++milli) {
            assert(ABoxMqttDigest_WriteMilli(milli, output, NULL)); putchar('\n');
        }
        return 0;
    }
    char input[4096]; uint8_t digest[32];
    size_t size = fread(input, 1, sizeof(input), stdin);
    if (size == sizeof(input)) return 2;
    cJSON *v = ABoxMqttDigest_Parse(input, size);
    if (!v) return 2;
    int ok = !strcmp(argv[1], "request") ? ABoxMqttDigest_Request(v, NULL, digest) : ABoxMqttDigest_Value(v, digest);
    if (!ok) { cJSON_Delete(v); return 2; }
    for (unsigned i = 0; i < 32; ++i) printf("%02x", digest[i]);
    putchar('\n'); ok = ABoxMqttDigest_Canonical(v, output, NULL); putchar('\n');
    cJSON_Delete(v); return ok ? 0 : 2;
}
