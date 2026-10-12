#ifndef ABOX_CLOUD_RESPONSE_H
#define ABOX_CLOUD_RESPONSE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Decimal timestamp and result are already encoded by the caller. No heap,
 * floating point conversion or product identity is supplied by Platform. */
int ABoxCloudResponse_Encode(char *output, size_t capacity,
    const char *version, const char *timestamp, const char *product,
    const char *request_id, uint16_t code, const char *message,
    const char *result_json);
/* Encode the same envelope directly into caller storage. A callback writes
 * one complete JSON result, avoiding a second product-sized buffer. NULL
 * omits result for existing compact error responses; Encode still defaults
 * its NULL result_json to {}. The callback must NUL-terminate on success. */
typedef int (*ABoxCloudResponseResultEncoder)(void *, char *, size_t);
int ABoxCloudResponse_EncodeResult(char *output, size_t capacity,
    const char *version, const char *timestamp, const char *product,
    const char *request_id, uint16_t code, const char *message,
    ABoxCloudResponseResultEncoder encode_result, void *context);
#ifdef __cplusplus
}
#endif
#endif
