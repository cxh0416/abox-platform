#include "abox_enrollment_service.h"
#include "abox_app_service.h"
#include <assert.h>
#include <string.h>

static int needs;
static int result = -1;
static unsigned pauses;
static int power_done = 1, rx_failure;
static unsigned resets;
static ABoxEnrollmentService *enrolling;
static ABoxEc800Rx rx;
void EC_Power_Task(void) { }
int EC_Power_IsOnDone(void) { return power_done; }
void EC_Power_StartRestartSequence(void) { ++resets; power_done = 0; }
void ABoxBootV2App_Task(void) { }
int ABoxBootV2App_IsBusy(void) { return 0; }
static int restart_rx(void *u)
{ (void)u; if (rx_failure) return 0; ABoxEc800Rx_AfterRestart(&rx); return 1; }
static int idle(void *u) { (void)u; return 0; }
static void cloud(void *u, uint32_t t) { (void)u; (void)t; }
static void enroll_poll(void *u, uint32_t t)
{ (void)u; (void)t; ABoxEnrollmentService_Poll(enrolling); }
static void network_event(void *u, ABoxAppServiceEvent e) { (void)u; (void)e; }
static void modem_event(ABoxEc800Event e, const uint8_t *d, uint16_t n, void *u)
{
    (void)u;
    if (e == ABOX_EC800_EVENT_LINE && n == 3 && !memcmp(d, "RDY", 3))
        ABoxEnrollmentService_AfterModemReset(enrolling);
}
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
    /* Drive the real shared owner, rather than manually clearing quarantine.
     * The same approved binding must survive both HTTP and UART failures. */
    ABoxAppService app;
    ABoxEc800Recovery recovery;
    uint8_t dma[ABOX_EC800_RX_DMA_MIN], ring[ABOX_EC800_RX_RING_MIN];
    ABoxAppServicePort app_port = {.restart_rx=restart_rx, .cloud=cloud,
        .enrollment=enroll_poll, .event=network_event, .recovery=&recovery,
        .transport_error=idle, .maintenance_busy=idle};
    enrolling=&service;
    assert(ABoxEc800At_Register(&at, 21, modem_event, NULL));
    assert(ABoxEc800Rx_Init(&rx, dma, sizeof(dma), ring, sizeof(ring)));
    ABoxEc800Recovery_Init(&recovery, 30000);
    assert(ABoxAppService_Init(&app, &at, &rx, &app_port));
    ABoxAppService_Poll(&app, 0);
    assert(resets == 1 && app.reset_pending && at.quarantined);
    power_done=1; rx_failure=1;
    ABoxAppService_Poll(&app, 1);
    assert(service.http.blocked && app.reset_pending && at.quarantined);
    rx_failure=0;
    ABoxAppService_Poll(&app, 129);
    assert(!app.reset_pending && resets == 1);
    assert(!at.quarantined && !service.http.blocked);
    assert(service.enrollment.have_request && !strcmp(request_id, "approved-request"));
    assert(!strcmp(poll_token, "approved-token"));
    return 0;
}
