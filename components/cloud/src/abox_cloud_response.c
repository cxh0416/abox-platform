#include "abox_cloud_response.h"
#include <stdio.h>
#include <string.h>

typedef struct { char *data; size_t capacity, used; int valid; } Writer;
static void append(Writer *w, const char *s)
{
    size_t n = strlen(s);
    if (!w->valid || n >= w->capacity - w->used) { w->valid = 0; return; }
    memcpy(w->data + w->used, s, n + 1U);
    w->used += n;
}
static void quoted(Writer *w, const char *s)
{
    char escaped[7];
    append(w, "\"");
    if (!s) s = "";
    while (*s && w->valid) {
        unsigned char c = (unsigned char)*s++;
        switch (c) {
        case '"': append(w, "\\\""); break;
        case '\\': append(w, "\\\\"); break;
        case '\b': append(w, "\\b"); break;
        case '\f': append(w, "\\f"); break;
        case '\n': append(w, "\\n"); break;
        case '\r': append(w, "\\r"); break;
        case '\t': append(w, "\\t"); break;
        default:
            if (c < 32U) { (void)snprintf(escaped, sizeof(escaped), "\\u%04X", c); append(w, escaped); }
            else { escaped[0] = (char)c; escaped[1] = 0; append(w, escaped); }
        }
    }
    append(w, "\"");
}
int ABoxCloudResponse_EncodeResult(char *output, size_t capacity,
    const char *version, const char *timestamp, const char *product,
    const char *request_id, uint16_t code, const char *message,
    ABoxCloudResponseResultEncoder encode_result, void *context)
{
    Writer w = {output, capacity, 0U, 1};
    char number[6];
    const char *p;
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!version || !timestamp || !*timestamp) return 0;
    for (p = timestamp; *p; ++p) if (*p < '0' || *p > '9') return 0;
    append(&w, "{\"version\":"); quoted(&w, version);
    append(&w, ",\"timestamp\":"); append(&w, timestamp);
    append(&w, ",\"data\":{");
    if (product) { append(&w, "\"product\":"); quoted(&w, product); append(&w, ","); }
    append(&w, "\"requestId\":"); quoted(&w, request_id);
    (void)snprintf(number, sizeof(number), "%u", (unsigned)code);
    append(&w, ",\"code\":"); append(&w, number);
    append(&w, ",\"msg\":"); quoted(&w, message);
    if (encode_result) {
        char *end;
        append(&w, ",\"result\":");
        if (w.valid && encode_result(context, output + w.used, capacity - w.used) &&
            (end = memchr(output + w.used, 0, capacity - w.used)) != 0)
            w.used = (size_t)(end - output);
        else w.valid = 0;
    }
    append(&w, "}}");
    if (!w.valid) output[0] = 0;
    return w.valid;
}
static int raw_result(void *context, char *output, size_t capacity)
{
    const char *json = (const char *)context;
    size_t size = strlen(json);
    if (size >= capacity) return 0;
    memcpy(output, json, size + 1U);
    return 1;
}
int ABoxCloudResponse_Encode(char *output, size_t capacity,
    const char *version, const char *timestamp, const char *product,
    const char *request_id, uint16_t code, const char *message,
    const char *result_json)
{
    return ABoxCloudResponse_EncodeResult(output, capacity, version, timestamp,
        product, request_id, code, message, raw_result,
        (void *)(result_json ? result_json : "{}"));
}
