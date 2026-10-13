#include "abox_mqtt_digest.h"
#include "abox_mqtt_v4.h"
#include "abox_sha256.h"
#include <math.h>
#include <string.h>

#define SAFE_INTEGER 9007199254740991.0
#define MAX_DEPTH 16U

/* Strict UTF-8 -> UTF-16 code units, for RFC 8785 property ordering. */
typedef struct { const unsigned char *next; uint16_t low; int invalid; } Units;
static uint32_t unit(Units *s)
{
    if (s->low) { uint32_t v = s->low; s->low = 0; return v; }
    uint32_t v = *s->next++, min = 0; unsigned n = 0;
    if (v == 0) { --s->next; return 0; }
    if (v < 0x80) return v;
    if (v >= 0xc2 && v <= 0xdf) { v &= 31; n = 1; min = 0x80; }
    else if (v >= 0xe0 && v <= 0xef) { v &= 15; n = 2; min = 0x800; }
    else if (v >= 0xf0 && v <= 0xf4) { v &= 7; n = 3; min = 0x10000; }
    else { s->invalid = 1; return 0; }
    while (n--) {
        unsigned c = *s->next;
        if ((c & 0xc0) != 0x80) { s->invalid = 1; return 0; }
        ++s->next; v = (v << 6) | (c & 63);
    }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) {
        s->invalid = 1; return 0;
    }
    if (v > 0xffff) { v -= 0x10000; s->low = (uint16_t)(0xdc00 | (v & 1023)); return 0xd800 | (v >> 10); }
    return v;
}
static int valid_string(const char *str)
{
    if (!str) return 0;
    Units s = {(const unsigned char *)str, 0, 0};
    while (unit(&s)) {}
    return !s.invalid;
}
static int compare(const char *a, const char *b)
{
    Units x = {(const unsigned char *)a, 0, 0}, y = {(const unsigned char *)b, 0, 0};
    for (;;) { uint32_t u = unit(&x), v = unit(&y);
        if (u != v) return u < v ? -1 : 1;
        if (!u) return 0;
    }
}
static int quoted(const char *s, ABoxCanonicalWriter write, void *ctx)
{
    static const char hex[] = "0123456789abcdef";
    if (!valid_string(s) || !write(ctx, "\"", 1)) return 0;
    const char *start = s;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\' || c < 32) {
            char escaped[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]}; size_t n = 6;
            if (s != start && !write(ctx, start, (size_t)(s - start))) return 0;
            if (c == '"' || c == '\\') { escaped[1] = (char)c; n = 2; }
            else {
                const char *short_escape = c == 8 ? "b" : c == 9 ? "t" : c == 10 ? "n" : c == 12 ? "f" : c == 13 ? "r" : NULL;
                if (short_escape) { escaped[1] = *short_escape; n = 2; }
            }
            if (!write(ctx, escaped, n)) return 0;
            start = s + 1;
        }
        ++s;
    }
    return (s == start || write(ctx, start, (size_t)(s - start))) && write(ctx, "\"", 1);
}
static int canonical(const cJSON *v, ABoxCanonicalWriter write, void *ctx, unsigned depth)
{
    if (!v || depth > MAX_DEPTH) return 0;
    if (cJSON_IsNull(v)) return write(ctx, "null", 4);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? write(ctx, "true", 4) : write(ctx, "false", 5);
    if (cJSON_IsString(v)) return quoted(v->valuestring, write, ctx);
    if (cJSON_IsNumber(v)) {
        double d = v->valuedouble;
        if (!isfinite(d) || fabs(d) > SAFE_INTEGER || (d == 0 && signbit(d))) return 0;
        int64_t number = (int64_t)d;
        if ((double)number != d) return 0;
        char text[18];
        int n = ABoxMqttV4_FormatInteger(text, sizeof(text), (uint64_t)(number < 0 ? -number : number));
        return n && (number >= 0 || write(ctx, "-", 1)) && write(ctx, text, strlen(text));
    }
    if (cJSON_IsArray(v)) {
        if (!write(ctx, "[", 1)) return 0;
        for (const cJSON *p = v->child; p; p = p->next) {
            if ((p != v->child && !write(ctx, ",", 1)) || !canonical(p, write, ctx, depth + 1)) return 0;
        }
        return write(ctx, "]", 1);
    }
    if (cJSON_IsObject(v)) {
        for (const cJSON *p = v->child; p; p = p->next) {
            if (!valid_string(p->string)) return 0;
            for (const cJSON *q = p->next; q; q = q->next)
                if (!q->string || !strcmp(p->string, q->string)) return 0;
        }
        if (!write(ctx, "{", 1)) return 0;
        const cJSON *previous = NULL;
        for (;;) {
            const cJSON *next = NULL;
            for (const cJSON *p = v->child; p; p = p->next)
                if ((!previous || compare(p->string, previous->string) > 0) &&
                    (!next || compare(p->string, next->string) < 0)) next = p;
            if (!next) break;
            if ((previous && !write(ctx, ",", 1)) || !quoted(next->string, write, ctx) ||
                !write(ctx, ":", 1) || !canonical(next, write, ctx, depth + 1)) return 0;
            previous = next;
        }
        return write(ctx, "}", 1);
    }
    return 0;
}
int ABoxMqttDigest_Canonical(const cJSON *value, ABoxCanonicalWriter write, void *context)
{ return write && canonical(value, write, context, 0); }
static int discard(void *ctx, const char *s, size_t n) { (void)ctx; (void)s; (void)n; return 1; }
cJSON *ABoxMqttDigest_Parse(const char *json, size_t length)
{
    if (!json || !length || memchr(json, 0, length)) return NULL;
    /* cJSON handles JSON grammar; retain wire numeric lexemes before decoding. */
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        if (json[i] == '"') {
            while (++i < length && json[i] != '"') {
                if ((unsigned char)json[i] < 32) return NULL;
                if (json[i] == '\\') {
                    if (++i >= length) return NULL;
                    if (json[i] == 'u' && i + 4 < length && !memcmp(json + i + 1, "0000", 4)) return NULL;
                }
            }
            if (i == length) return NULL;
        } else if (json[i] == '{' || json[i] == '[') {
            if (++depth > MAX_DEPTH) return NULL;
        } else if (json[i] == '}' || json[i] == ']') {
            if (!depth) return NULL;
            --depth;
        } else if (json[i] == '-' || (json[i] >= '0' && json[i] <= '9')) {
            size_t start = i;
            if (json[i] == '-' && ++i == length) return NULL;
            if (json[i] < '0' || json[i] > '9') return NULL;
            if (i > start && json[i] == '0') return NULL;
            if (json[i] == '0' && i + 1 < length && json[i + 1] >= '0' && json[i + 1] <= '9') return NULL;
            while (i + 1 < length && json[i + 1] >= '0' && json[i + 1] <= '9') ++i;
            if (i + 1 < length && (json[i + 1] == '.' || json[i + 1] == 'e' || json[i + 1] == 'E')) return NULL;
        }
    }
    const char *end = NULL;
    cJSON *v = cJSON_ParseWithLengthOpts(json, length, &end, 0);
    if (!v) return NULL;
    while ((size_t)(end - json) < length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    if ((size_t)(end - json) != length || !canonical(v, discard, NULL, 0)) { cJSON_Delete(v); return NULL; }
    return v;
}
static int hash(void *ctx, const char *s, size_t n)
{ ABoxSha256_Update(ctx, (const uint8_t *)s, (uint32_t)n); return 1; }
int ABoxMqttDigest_Value(const cJSON *value, uint8_t digest[32])
{
    ABoxSha256 ctx;
    if (!digest) return 0;
    ABoxSha256_Init(&ctx);
    if (!ABoxMqttDigest_Canonical(value, hash, &ctx)) { memset(&ctx, 0, sizeof(ctx)); memset(digest, 0, 32); return 0; }
    ABoxSha256_Final(&ctx, digest); return 1;
}
int ABoxMqttDigest_RequestFields(const char *command, const cJSON *params,
                               const cJSON *expires, uint8_t digest[32])
{
    ABoxSha256 ctx;
    if (!digest) return 0;
    ABoxSha256_Init(&ctx);
    int ok = command && params && (!expires || cJSON_IsNumber(expires)) &&
        hash(&ctx, "{\"command\":", 11) && quoted(command, hash, &ctx) &&
        (!expires || (hash(&ctx, ",\"expiresAt\":", 13) && canonical(expires, hash, &ctx, 1))) &&
        hash(&ctx, ",\"params\":", 10) && canonical(params, hash, &ctx, 1) && hash(&ctx, "}", 1);
    if (!ok) { memset(&ctx, 0, sizeof(ctx)); memset(digest, 0, 32); return 0; }
    ABoxSha256_Final(&ctx, digest); return 1;
}
int ABoxMqttDigest_Request(const cJSON *data, const cJSON *normalized_params, uint8_t digest[32])
{
    if (!digest) return 0;
    memset(digest, 0, 32);
    if (!cJSON_IsObject(data) || !canonical(data, discard, NULL, 0)) return 0;
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(data, "command");
    const cJSON *params = normalized_params ? normalized_params : cJSON_GetObjectItemCaseSensitive(data, "params");
    const cJSON *expires = cJSON_GetObjectItemCaseSensitive(data, "expiresAt");
    if (!cJSON_IsString(command) || !params || (expires && !cJSON_IsNumber(expires))) return 0;
    return ABoxMqttDigest_RequestFields(command->valuestring, params, expires, digest);
}
