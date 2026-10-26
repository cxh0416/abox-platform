#include "abox_enrollment_service.h"
#include <string.h>

int ABoxEnrollmentService_RequirePsk(ABoxEnrollmentService *s,
                                    const uint8_t *ca_pem, uint32_t length)
{
    if (!s || !s->initialized) return 0;
    if (ABoxEnrollmentEc800Http_SetCaPem(&s->http, ca_pem, length) &&
        ABoxEnrollment_RequirePsk(&s->enrollment)) return 1;
    (void)ABoxEc800At_Register(s->http.at, s->http.owner, NULL, NULL);
    s->http.at = NULL;
    s->initialized = 0U;
    return 0;
}

static int ready(void *user)
{
    ABoxEnrollmentService *s = (ABoxEnrollmentService *)user;
    return s->port.network_ready(s->port.user) &&
           ABoxEnrollmentEc800Http_Ready(&s->http);
}
static int identity(void *user, ABoxEnrollmentIdentity *out)
{
    ABoxEnrollmentService *s = (ABoxEnrollmentService *)user;
    return s->port.identity(s->port.user, out);
}
static int post(void *user, const char *url, const char *body,
                char *response, size_t capacity)
{
    ABoxEnrollmentService *s = (ABoxEnrollmentService *)user;
    s->port.mqtt_pause(s->port.user, 1U);
    return ABoxEnrollmentEc800Http_Post(&s->http, url, body, response, capacity);
}
static void http_done(void *user, uint16_t status, size_t length, int cleanup_ok)
{
    ABoxEnrollmentService *s = (ABoxEnrollmentService *)user;
    ABoxEnrollment_OnHttp(&s->enrollment, status, length, cleanup_ok,
                          s->port.now_ms(s->port.user));
}
static void cancel_http(void *user)
{
    ABoxEnrollmentService *s = (ABoxEnrollmentService *)user;
    ABoxEnrollmentEc800Http_Cancel(&s->http);
}
static int start_trial(void *user, const ABoxEnrollmentCredential *credential)
{
    ABoxEnrollmentService *s = (ABoxEnrollmentService *)user;
    return s->port.start_trial(s->port.user, credential);
}
static void cancel_trial(void *user)
{
    ABoxEnrollmentService *s = (ABoxEnrollmentService *)user;
    s->port.cancel_trial(s->port.user);
}
int ABoxEnrollmentService_Init(ABoxEnrollmentService *s, ABoxEc800At *at,
                               ABoxEc800Owner owner, const char *ca_file,
                               const char *origin,
                               const ABoxEnrollmentBuffers *buffers,
                               uint8_t tls_profile_id,
                               const ABoxEnrollmentServicePort *port)
{
    ABoxEnrollmentPort p;
    if (!s || !at || !ca_file || !origin || !buffers || !port ||
        !port->now_ms || !port->network_ready || !port->needs_enrollment ||
        !port->identity || !port->mqtt_pause || !port->start_trial ||
        !port->trial_result || !port->cancel_trial) return 0;
    memset(s, 0, sizeof(*s));
    s->port = *port;
    p = (ABoxEnrollmentPort){s, ready, identity, post, cancel_http,
                             start_trial, cancel_trial};
    /* Validate the local contract before claiming an AT event slot. */
    if (!ABoxEnrollment_Init(&s->enrollment, &p, buffers, origin) ||
        !ABoxEnrollment_SetExpectedTlsProfile(&s->enrollment,
                                               tls_profile_id) ||
        !ABoxEnrollmentEc800Http_Init(&s->http, at, owner, ca_file,
                                       http_done, s)) return 0;
    s->initialized = 1U;
    if (port->needs_enrollment(port->user)) port->mqtt_pause(port->user, 1U);
    return 1;
}
void ABoxEnrollmentService_Poll(ABoxEnrollmentService *s)
{
    int result;
    if (!s || !s->initialized) return;
    /* Unproven HTTP cleanup/CA IO cannot be retried on the same wire.
     * Signal the existing owner recovery instead of waiting forever behind
     * Http_Ready. Physical reset clears the block and preserves request ID. */
    if (s->http.blocked) {
        if (!s->http.at->quarantined) ABoxEc800At_Quarantine(s->http.at);
        return;
    }
    if (ABoxEnrollment_State(&s->enrollment) == ABOX_ENROLLMENT_TRIAL) {
        result = s->port.trial_result(s->port.user);
        if (result >= 0)
            ABoxEnrollment_TrialResult(&s->enrollment, result != 0,
                s->port.now_ms(s->port.user));
        return;
    }
    if (!s->port.needs_enrollment(s->port.user)) return;
    s->port.mqtt_pause(s->port.user, 1U);
    if (s->port.network_ready(s->port.user))
        ABoxEnrollmentEc800Http_PrepareCa(&s->http);
    ABoxEnrollment_Poll(&s->enrollment, s->port.now_ms(s->port.user));
}
ABoxEnrollmentState ABoxEnrollmentService_State(const ABoxEnrollmentService *s)
{
    return s ? ABoxEnrollment_State(&s->enrollment) : ABOX_ENROLLMENT_WAITING;
}
void ABoxEnrollmentService_Cancel(ABoxEnrollmentService *s)
{
    if (s && s->initialized) ABoxEnrollment_Cancel(&s->enrollment);
}
void ABoxEnrollmentService_AfterModemReset(ABoxEnrollmentService *s)
{
    if (!s || !s->initialized) return;
    ABoxEnrollmentEc800Http_AfterModemReset(&s->http);
    /* Covers an HTTP state whose AT operation had already disappeared. This
     * is inert after http_done and preserves the existing request binding. */
    ABoxEnrollment_OnHttp(&s->enrollment, 0U, 0U, 1,
                          s->port.now_ms(s->port.user));
}
