#include "abox_mqtt_digest.h"
#include "abox_sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    if (argc == 1) { check_sha(); return 0; }
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
