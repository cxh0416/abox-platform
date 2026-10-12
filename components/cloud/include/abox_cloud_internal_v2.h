#ifndef ABOX_CLOUD_INTERNAL_V2_H
#define ABOX_CLOUD_INTERNAL_V2_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "abox_ec800_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared envelope for the legacy internal Topic V2 request contract. Products
 * still validate params, choose actions, and map errors to their own codes. */
typedef struct {
    cJSON *root;
    cJSON *params;
    const char *request_id;
    cJSON *command;
} ABoxCloudInternalV2Envelope;

typedef struct {
    cJSON *command, *params;
    const char *request_id;
} ABoxCloudInternalV2Fields;
typedef enum {
    ABOX_INTERNAL_V2_STRICT,
    /* Existing case-insensitive lookup, bounded (possibly empty) ID, numeric
     * timestamp and action-owned params validation. No wire format change. */
    ABOX_INTERNAL_V2_TYPED_TIMESTAMP_COMPAT
} ABoxCloudInternalV2Policy;
typedef enum {
    ABOX_INTERNAL_V2_OK,
    ABOX_INTERNAL_V2_INVALID_DATA,
    ABOX_INTERNAL_V2_INVALID_ID,
    ABOX_INTERNAL_V2_INVALID_VERSION_TIMESTAMP,
    ABOX_INTERNAL_V2_INVALID_COMMAND,
    ABOX_INTERNAL_V2_INVALID_PARAMS,
    ABOX_INTERNAL_V2_INVALID_PRODUCT
} ABoxCloudInternalV2Status;
/* Inspect an already parsed root. Fields borrow its lifetime; this function
 * never frees it. Failure stages let each product keep its existing errors. */
ABoxCloudInternalV2Status ABoxCloudInternalV2_Read(cJSON *root,
    const char *expected_product, size_t request_id_capacity,
    ABoxCloudInternalV2Policy policy, ABoxCloudInternalV2Fields *fields);

int ABoxCloudRequestId_Valid(const char *value, size_t capacity);
/* Recover an unescaped requestId from malformed JSON for a product's existing
 * error response. This is deliberately bounded and does not decode escapes. */
void ABoxCloudRequestId_ExtractRaw(const char *payload, size_t length,
                                   char *output, size_t capacity);
/* expected_product=NULL preserves products that do not validate the optional
 * data.product field. The returned pointers live until Close. */
int ABoxCloudInternalV2_Open(const char *payload, size_t length,
                             const char *expected_product,
                             size_t request_id_capacity,
                             ABoxCloudInternalV2Envelope *envelope);
/* Parse the internal rotation fields only; transport/trial policy stays with caller. */
int ABoxCloudInternalV2_PskCredentials(const cJSON *params, ABoxTlsCredentials *output);
void ABoxCloudInternalV2_Close(ABoxCloudInternalV2Envelope *envelope);

#ifdef __cplusplus
}
#endif

#endif
