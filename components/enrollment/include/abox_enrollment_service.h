#ifndef ABOX_ENROLLMENT_SERVICE_H
#define ABOX_ENROLLMENT_SERVICE_H

#include "abox_enrollment.h"
#include "abox_enrollment_ec800_http.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Product policy and storage stay outside the service. trial_result returns
 * -1 while pending, 0 on terminal failure (including restore failure), 1 after
 * commit and persistent readback. Reporting failure never releases Runtime's
 * safety locks. All callbacks run in the EC800 owner context. */
typedef struct {
    void *user;
    uint32_t (*now_ms)(void *);
    int (*network_ready)(void *);
    int (*needs_enrollment)(void *);
    int (*identity)(void *, ABoxEnrollmentIdentity *);
    void (*mqtt_pause)(void *, uint8_t);
    int (*start_trial)(void *, const ABoxEnrollmentCredential *);
    int (*trial_result)(void *);
    void (*cancel_trial)(void *);
} ABoxEnrollmentServicePort;

typedef struct {
    ABoxEnrollment enrollment;
    ABoxEnrollmentEc800Http http;
    ABoxEnrollmentServicePort port;
    uint8_t initialized;
} ABoxEnrollmentService;

int ABoxEnrollmentService_Init(ABoxEnrollmentService *service, ABoxEc800At *at,
                               ABoxEc800Owner owner, const char *ca_file,
                               const char *origin,
                               const ABoxEnrollmentBuffers *buffers,
                               uint8_t tls_profile_id,
                               const ABoxEnrollmentServicePort *port);
void ABoxEnrollmentService_Poll(ABoxEnrollmentService *service);
/* Configure before the first Poll. Failure releases the event slot and keeps
 * this service unavailable; it never falls back to enrollment without a CA. */
int ABoxEnrollmentService_RequirePsk(ABoxEnrollmentService *service,
                                    const uint8_t *ca_pem, uint32_t length);
ABoxEnrollmentState ABoxEnrollmentService_State(const ABoxEnrollmentService *service);
void ABoxEnrollmentService_Cancel(ABoxEnrollmentService *service);
void ABoxEnrollmentService_AfterModemReset(ABoxEnrollmentService *service);

/* Shared request/response correlation for an explicitly authorized PSK
 * migration. Products retain their own safety admission and MQTT PUBACK
 * tracking. A response is confirmed only after the 202 was delivered. */
typedef struct {
    uint32_t started_ms;
    uint8_t waiting, active;
} ABoxEnrollmentMigration;
#define ABOX_ENROLLMENT_MIGRATION_RESPONSE_MS 15000U
#define ABOX_ENROLLMENT_MIGRATION_APPROVAL_MS 600000U
int ABoxEnrollmentMigration_Request(ABoxEnrollmentMigration *, ABoxEnrollmentService *,
    char *pending_id, size_t capacity, const char *request_id, uint32_t now);
int ABoxEnrollmentMigration_Confirm(ABoxEnrollmentMigration *, ABoxEnrollmentService *,
    char *pending_id, size_t capacity, const char *request_id, uint8_t admitted, uint32_t now);
/* Cancels expired approval only when the product's trial and HTTP are idle.
 * Returns 1 for response timeout, 2 for approval timeout, otherwise 0. */
int ABoxEnrollmentMigration_Poll(ABoxEnrollmentMigration *, ABoxEnrollmentService *,
    char *pending_id, size_t capacity, uint8_t trial_active, uint32_t now);
void ABoxEnrollmentMigration_Finish(ABoxEnrollmentMigration *);


#ifdef __cplusplus
}
#endif
#endif
