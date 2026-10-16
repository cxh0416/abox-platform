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
int ABoxMqttDigest_WriteString(const char *value, ABoxCanonicalWriter write, void *context)
{ return write && quoted(value, write, context); }
int ABoxMqttDigest_WriteInteger(int64_t value, ABoxCanonicalWriter write, void *context)
{
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
    char text[18];
    return write && magnitude <= 9007199254740991ULL &&
        ABoxMqttV4_FormatInteger(text, sizeof(text), magnitude) &&
        (value >= 0 || write(context, "-", 1)) && write(context, text, strlen(text));
}
int ABoxMqttDigest_WriteMilli(int32_t value, ABoxCanonicalWriter write, void *context)
{
    uint32_t magnitude = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    uint32_t fraction = magnitude % 1000U;
    char decimal[4] = {'.', (char)('0' + fraction / 100U),
        (char)('0' + fraction / 10U % 10U), (char)('0' + fraction % 10U)};
    size_t n = sizeof(decimal);
    while (n > 1 && decimal[n - 1] == '0') --n;
    return write && (value >= 0 || write(context, "-", 1)) &&
        ABoxMqttDigest_WriteInteger(magnitude / 1000U, write, context) &&
        (!fraction || write(context, decimal, n));
}
static int canonical(const cJSON *v, ABoxCanonicalWriter write, void *ctx, unsigned depth)
{
    if (!v || depth > MAX_DEPTH) return 0;
    if (cJSON_IsNull(v)) return write(ctx, "null", 4);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? write(ctx, "true", 4) : write(ctx, "false", 5);
    if (cJSON_IsString(v)) return quoted(v->valuestring, write, ctx);
    if (cJSON_IsNumber(v)) {
        return ABoxMqttDigest_WriteNumber(v->valuedouble, write, ctx);
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
typedef struct { size_t end; int integer, nonzero; } NumberToken;
static int digit(char ch) { return ch >= '0' && ch <= '9'; }
static int number_token(const char *json, size_t length, size_t start, NumberToken *token)
{
    size_t i = start;
    token->integer = 1; token->nonzero = 0;
    if (json[i] == '-' && ++i == length) return 0;
    if (!digit(json[i])) return 0;
    if (json[i] == '0' && i + 1 < length && digit(json[i + 1])) return 0;
    do { token->nonzero |= json[i++] != '0'; } while (i < length && digit(json[i]));
    if (i < length && json[i] == '.') {
        token->integer = 0;
        if (++i == length || !digit(json[i])) return 0;
        do { token->nonzero |= json[i++] != '0'; } while (i < length && digit(json[i]));
    }
    if (i < length && (json[i] == 'e' || json[i] == 'E')) {
        token->integer = 0;
        if (++i == length) return 0;
        if ((json[i] == '+' || json[i] == '-') && ++i == length) return 0;
        if (!digit(json[i])) return 0;
        do { ++i; } while (i < length && digit(json[i]));
    }
    if (i < length && json[i] != ',' && json[i] != '}' && json[i] != ']' &&
        json[i] != ' ' && json[i] != '\t' && json[i] != '\r' && json[i] != '\n') return 0;
    token->end = i;
    return 1;
}
typedef struct {
    const char *json; size_t length, cursor;
    const cJSON *root; ABoxMqttNumberField is_number; void *context;
} WireNumbers;
static int validate_numbers(const cJSON *value, WireNumbers *wire, unsigned depth)
{
    if (depth > MAX_DEPTH) return 0;
    if (cJSON_IsNumber(value)) {
        size_t i = wire->cursor;
        /* JSON tree iteration retains input order. Match each numeric node to
         * its original token so 1.0 cannot become a legal Integer after strtod. */
        for (; i < wire->length; ++i) {
            if (wire->json[i] == '"') {
                while (++i < wire->length && wire->json[i] != '"')
                    if (wire->json[i] == '\\') ++i;
            } else if (wire->json[i] == '-' || digit(wire->json[i])) break;
        }
        NumberToken token;
        if (i == wire->length || !number_token(wire->json, wire->length, i, &token)) return 0;
        wire->cursor = token.end;
        double d = value->valuedouble;
        if (!isfinite(d) || (d == 0 && (signbit(d) || token.nonzero || wire->json[i] == '-'))) return 0;
        if (wire->is_number && wire->is_number(wire->context, wire->root, value)) return 1;
        if (!token.integer) return 0;
        /* Check raw digits before conversion: 9007199254740991.1 must never
         * round into the Integer domain, nor can an unsafe integer do so. */
        uint64_t magnitude = 0;
        if (wire->json[i] == '-') ++i;
        for (; i < token.end; ++i) {
            unsigned v = (unsigned)(wire->json[i] - '0');
            if (magnitude > (9007199254740991ULL - v) / 10U) return 0;
            magnitude = magnitude * 10U + v;
        }
        return fabs(d) <= SAFE_INTEGER && (double)(int64_t)d == d;
    }
    for (const cJSON *p = value->child; p; p = p->next)
        if (!validate_numbers(p, wire, depth + 1)) return 0;
    return 1;
}
cJSON *ABoxMqttDigest_ParseProfile(const char *json, size_t length,
                                 ABoxMqttNumberField is_number, void *context)
{
    if (!json || !length || memchr(json, 0, length)) return NULL;
    if ((unsigned char)json[0] == 0xef) return NULL; /* No UTF-8 BOM. */
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
            NumberToken token;
            if (!number_token(json, length, i, &token)) return NULL;
            i = token.end - 1;
        } else if ((unsigned char)json[i] <= 32 && json[i] != ' ' && json[i] != '\t' &&
                   json[i] != '\r' && json[i] != '\n') {
            return NULL;
        }
    }
    const char *end = NULL;
    cJSON *v = cJSON_ParseWithLengthOpts(json, length, &end, 0);
    if (!v) return NULL;
    while ((size_t)(end - json) < length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    WireNumbers wire = {json, length, 0, v, is_number, context};
    if ((size_t)(end - json) != length || !validate_numbers(v, &wire, 0) ||
        !canonical(v, discard, NULL, 0)) { cJSON_Delete(v); return NULL; }
    return v;
}
cJSON *ABoxMqttDigest_Parse(const char *json, size_t length)
{ return ABoxMqttDigest_ParseProfile(json, length, NULL, NULL); }
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
int ABoxMqttDigest_WriteRequest(const char *command, int has_expires, uint64_t expires,
    ABoxCanonicalEncoder params, void *value, ABoxCanonicalWriter write, void *context)
{
    return write && command && params && (!has_expires || expires <= 9007199254740991ULL) &&
        write(context, "{\"command\":", 11) && quoted(command, write, context) &&
        (!has_expires || (write(context, ",\"expiresAt\":", 13) &&
                          ABoxMqttDigest_WriteInteger((int64_t)expires, write, context))) &&
        write(context, ",\"params\":", 10) && params(value, write, context) && write(context, "}", 1);
}
int ABoxMqttDigest_RequestStream(const char *command, int has_expires, uint64_t expires,
    ABoxCanonicalEncoder params, void *value, uint8_t digest[32])
{
    ABoxSha256 ctx;
    if (!digest) return 0;
    ABoxSha256_Init(&ctx);
    int ok = ABoxMqttDigest_WriteRequest(command, has_expires, expires, params, value, hash, &ctx);
    if (!ok) { memset(&ctx, 0, sizeof(ctx)); memset(digest, 0, 32); return 0; }
    ABoxSha256_Final(&ctx, digest); return 1;
}
static int tree(void *value, ABoxCanonicalWriter write, void *context)
{ return canonical(value, write, context, 1); }
static int null_params(void *value, ABoxCanonicalWriter write, void *context)
{ (void)value; return write(context, "null", 4); }
int ABoxMqttDigest_NullRequest(const char *command, int has_expires, uint64_t expires, uint8_t digest[32])
{ return ABoxMqttDigest_RequestStream(command, has_expires, expires, null_params, NULL, digest); }
int ABoxMqttDigest_RequestFields(const char *command, const cJSON *params,
                               const cJSON *expires, uint8_t digest[32])
{
    if (!params || (expires && (!cJSON_IsNumber(expires) || !isfinite(expires->valuedouble) || expires->valuedouble < 0 ||
        expires->valuedouble > SAFE_INTEGER || (double)(uint64_t)expires->valuedouble != expires->valuedouble ||
        (expires->valuedouble == 0 && signbit(expires->valuedouble))))) {
        if (digest) memset(digest, 0, 32);
        return 0;
    }
    return ABoxMqttDigest_RequestStream(command, expires != NULL,
        expires ? (uint64_t)expires->valuedouble : 0, tree, (void *)params, digest);
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
