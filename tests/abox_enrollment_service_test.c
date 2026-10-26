#include "abox_enrollment_service.h"
#include <assert.h>
#include <string.h>

static int needs;
static int result = -1;
static unsigned pauses;
static uint32_t now_ms(void *user) { (void)user; return 100U; }
static int network_ready(void *user) { (void)user; return 0; }
static int needs_enrollment(void *user) { (void)user; return needs; }
static int identity(void *user, ABoxEnrollmentIdentity *out)
{ (void)user; (void)out; return 0; }
static void mqtt_pause(void *user, uint8_t pause)
{ (void)user; pauses += pause; }
static int start_trial(void *user, const ABoxEnrollmentCredential *credential)
{ (void)user; (void)credential; return 1; }
static int trial_result(void *user) { (void)user; return result; }
static void cancel_trial(void *user) { (void)user; }
static uint32_t tick(void *user) { (void)user; return 100U; }
static int write_data(void *user, const uint8_t *data, uint16_t length,
                      uint32_t timeout)
{ (void)user; (void)data; (void)length; (void)timeout; return 1; }

int main(void)
{
    ABoxEnrollmentService service;
    ABoxEc800At at;
    ABoxEc800AtPort at_port = {0};
    ABoxEnrollmentServicePort port = {0};
    char url[192], body[512], response[769], request_id[48], poll_token[64];
    ABoxEnrollmentBuffers buffers = {url, body, response, request_id,
                                      poll_token, sizeof(url), sizeof(body),
                                      sizeof(response), sizeof(request_id),
                                      sizeof(poll_token)};
    at_port.tick_ms = tick;
    at_port.write = write_data;
    assert(ABoxEc800At_Init(&at, &at_port));
    port.now_ms = now_ms;
    port.network_ready = network_ready;
    port.needs_enrollment = needs_enrollment;
    port.identity = identity;
    port.mqtt_pause = mqtt_pause;
    port.start_trial = start_trial;
    port.trial_result = trial_result;
    port.cancel_trial = cancel_trial;
    assert(ABoxEnrollmentService_Init(&service, &at,
        ABOX_EC800_OWNER_PRODUCT_BASE + 2U, "UFS:ota_ca.pem",
        "https://ota.example.test", &buffers, 0U, &port));
    assert(service.enrollment.expected_tls_profile_id == 0U);
    assert(pauses == 0U);
    ABoxEnrollmentService_Poll(&service);
    assert(pauses == 0U);
    needs = 1;
    ABoxEnrollmentService_Poll(&service);
    assert(pauses == 1U);
    service.enrollment.state = ABOX_ENROLLMENT_TRIAL;
    service.enrollment.waiting_trial = 1U;
    ABoxEnrollmentService_Poll(&service);
    assert(ABoxEnrollmentService_State(&service) == ABOX_ENROLLMENT_TRIAL);
    result = 1;
    ABoxEnrollmentService_Poll(&service);
    assert(ABoxEnrollmentService_State(&service) == ABOX_ENROLLMENT_COMPLETE);
    ABoxEnrollmentService_AfterModemReset(&service);
    service.enrollment.state = ABOX_ENROLLMENT_HTTP;
    service.enrollment.have_request = 1U;
    strcpy(request_id, "bound-request");
    strcpy(poll_token, "bound-token");
    ABoxEnrollmentService_AfterModemReset(&service);
    assert(service.enrollment.state == ABOX_ENROLLMENT_WAITING);
    service.enrollment.state = ABOX_ENROLLMENT_TRIAL;
    service.enrollment.waiting_trial = 1U;
    result = 0; /* product reports RESTORE_FAILED as a terminal failed trial */
    ABoxEnrollmentService_Poll(&service);
    assert(service.enrollment.state == ABOX_ENROLLMENT_WAITING && !service.enrollment.waiting_trial);
    assert(service.enrollment.have_request && !strcmp(request_id, "bound-request"));
    assert(!strcmp(poll_token, "bound-token"));
    ABoxEnrollmentService_AfterModemReset(&service);
    assert(service.enrollment.state == ABOX_ENROLLMENT_WAITING);
    service.enrollment.state = ABOX_ENROLLMENT_CANCELLING;
    ABoxEnrollmentService_AfterModemReset(&service);
    assert(!service.enrollment.have_request);
    assert(service.enrollment.state == ABOX_ENROLLMENT_WAITING);
    /* A delayed approval may outlive a failed HTTP cleanup. Network readiness
     * can be false; still request physical recovery without losing binding. */
    service.enrollment.have_request = 1U;
    strcpy(request_id, "approved-request");
    strcpy(poll_token, "approved-token");
    service.http.blocked = 1U;
    ABoxEnrollmentService_Poll(&service);
    assert(at.quarantined && service.http.blocked);
    uint32_t generation = at.generation;
    ABoxEnrollmentService_Poll(&service);
    assert(at.generation == generation); /* Request recovery only once. */
    ABoxEc800At_OnModemReset(&at);
    ABoxEnrollmentService_AfterModemReset(&service);
    assert(!at.quarantined && !service.http.blocked);
    assert(service.enrollment.have_request && !strcmp(request_id, "approved-request"));
    assert(!strcmp(poll_token, "approved-token"));
    return 0;
}
