#include "abox_cloud_service.h"

#include <string.h>

int ABoxCloudBootstrap_Init(ABoxCloudBootstrap *bootstrap)
{
    if (!bootstrap) return 0;
    memset(bootstrap, 0, sizeof(*bootstrap));
    bootstrap->generation = 1U;
    return 1;
}

void ABoxCloudBootstrap_NetworkChanged(ABoxCloudBootstrap *bootstrap)
{
    if (!bootstrap) return;
    if (++bootstrap->generation == 0U) bootstrap->generation = 1U;
    bootstrap->ready = 0U;
    bootstrap->stage = 0U;
    bootstrap->in_flight = ABOX_CLOUD_JOB_NONE;
    bootstrap->operation = 0U;
}

void ABoxCloudBootstrap_SetReady(ABoxCloudBootstrap *bootstrap, int ready)
{
    if (!bootstrap) return;
    if (!ready) {
        if (bootstrap->ready) ABoxCloudBootstrap_NetworkChanged(bootstrap);
        return;
    }
    if (bootstrap->ready) return;
    bootstrap->ready = 1U;
    bootstrap->stage = 1U;
}

int ABoxCloudBootstrap_Active(const ABoxCloudBootstrap *bootstrap)
{
    return bootstrap && bootstrap->ready && bootstrap->stage != 0U;
}

int ABoxCloudBootstrap_Next(const ABoxCloudBootstrap *bootstrap, ABoxCloudJob *job)
{
    if (!bootstrap || !job) return 0;
    memset(job, 0, sizeof(*job));
    if (!ABoxCloudBootstrap_Active(bootstrap) ||
        bootstrap->in_flight != ABOX_CLOUD_JOB_NONE) return 0;
    job->generation = bootstrap->generation;
    if (bootstrap->stage == 1U) job->kind = ABOX_CLOUD_JOB_MANIFEST;
    else if (bootstrap->stage == 2U) job->kind = ABOX_CLOUD_JOB_HEARTBEAT;
    else if (bootstrap->stage == 3U) {
        job->kind = ABOX_CLOUD_JOB_STATE;
        job->reason = bootstrap->ever_ready ? "reconnect" : "startup";
    } else return 0;
    return 1;
}

int ABoxCloudBootstrap_Begin(ABoxCloudBootstrap *bootstrap,
                             const ABoxCloudJob *job, uint64_t operation)
{
    ABoxCloudJob expected;
    if (!bootstrap || !job || !operation ||
        !ABoxCloudBootstrap_Next(bootstrap, &expected) ||
        job->kind != expected.kind || job->generation != expected.generation)
        return 0;
    bootstrap->in_flight = job->kind;
    bootstrap->operation = operation;
    return 1;
}

ABoxCloudJobKind ABoxCloudBootstrap_Receipt(ABoxCloudBootstrap *bootstrap,
                                            uint32_t generation,
                                            uint64_t operation, int confirmed)
{
    ABoxCloudJobKind kind;
    if (!bootstrap || !operation || generation != bootstrap->generation ||
        operation != bootstrap->operation ||
        bootstrap->in_flight == ABOX_CLOUD_JOB_NONE)
        return ABOX_CLOUD_JOB_NONE;
    kind = bootstrap->in_flight;
    bootstrap->in_flight = ABOX_CLOUD_JOB_NONE;
    bootstrap->operation = 0U;
    if (!confirmed) return ABOX_CLOUD_JOB_NONE;
    if (kind == ABOX_CLOUD_JOB_MANIFEST && bootstrap->stage == 1U)
        bootstrap->stage = 2U;
    else if (kind == ABOX_CLOUD_JOB_HEARTBEAT && bootstrap->stage == 2U)
        bootstrap->stage = 3U;
    else if (kind == ABOX_CLOUD_JOB_STATE && bootstrap->stage == 3U) {
        bootstrap->stage = 0U;
        bootstrap->ever_ready = 1U;
    }
    return kind;
}

int ABoxCloudTxTracker_Init(ABoxCloudTxTracker *tracker)
{
    if (!tracker) return 0;
    memset(tracker, 0, sizeof(*tracker));
    tracker->generation = 1U;
    return 1;
}

void ABoxCloudTxTracker_NetworkChanged(ABoxCloudTxTracker *tracker)
{
    if (!tracker) return;
    if (++tracker->generation == 0U) tracker->generation = 1U;
    tracker->operation = 0U;
    tracker->kind = 0U;
    tracker->ready = 0U;
}

void ABoxCloudTxTracker_SetReady(ABoxCloudTxTracker *tracker, int ready)
{
    if (!tracker) return;
    if (!ready) {
        if (tracker->ready) ABoxCloudTxTracker_NetworkChanged(tracker);
        return;
    }
    tracker->ready = 1U;
}

int ABoxCloudTxTracker_Begin(ABoxCloudTxTracker *tracker,
                             uint8_t kind, uint64_t operation)
{
    if (!tracker || !kind || !operation || tracker->kind) return 0;
    tracker->kind = kind;
    tracker->operation = operation;
    return 1;
}

ABoxCloudTxReceipt ABoxCloudTxTracker_Receipt(ABoxCloudTxTracker *tracker,
                                               uint32_t generation,
                                               uint64_t operation,
                                               int confirmed, uint8_t *kind)
{
    if (!tracker || !operation || !tracker->kind ||
        generation != tracker->generation || operation != tracker->operation)
        return ABOX_CLOUD_TX_IGNORED;
    if (kind) *kind = tracker->kind;
    tracker->kind = 0U;
    tracker->operation = 0U;
    return confirmed ? ABOX_CLOUD_TX_CONFIRMED : ABOX_CLOUD_TX_FAILED;
}

static int copy_text(char *to, unsigned capacity, const char *from)
{
    unsigned length;
    if (!to || !from || !capacity) return 0;
    for (length = 0U; from[length] != '\0'; ++length)
        if (length + 1U >= capacity) return 0;
    memcpy(to, from, length + 1U);
    return 1;
}

int ABoxCloudService_Init(ABoxCloudService *service,
                          const ABoxCloudServiceOptions *options, uint32_t now)
{
    if (!service || !options || !options->heartbeat_period_ms ||
        options->heartbeat_period_ms > INT32_MAX ||
        options->status_period_ms > INT32_MAX ||
        options->response_preempts_bootstrap > 1U) return 0;
    memset(service, 0, sizeof(*service));
    service->options = *options;
    service->generation = 1U;
    service->heartbeat_due = now;
    service->status_due = now;
    return 1;
}

void ABoxCloudService_SetStateCount(ABoxCloudService *s, uint8_t count)
{ if (s && !s->in_flight) { s->state_count=count?count:1U; s->state_index=0; s->periodic_state_count=0; } }
void ABoxCloudService_SetPeriodicStateCount(ABoxCloudService *s, uint8_t count)
{ if (s && !s->in_flight && count<=s->state_count) s->periodic_state_count=count; }
void ABoxCloudService_SetProductStatePending(ABoxCloudService *s, int pending)
{ if (s) s->product_state_pending=pending?1U:0U; }
void ABoxCloudService_NetworkChanged(ABoxCloudService *service)
{
    if (!service) return;
    service->generation++;
    if (!service->generation) service->generation = 1U;
    service->ready = 0U;
    service->bootstrap_stage = 0U;
    service->in_flight = ABOX_CLOUD_JOB_NONE;
    service->operation = 0U;
    service->state_index=0U; service->product_state_pending=0U;
    /* A confirmed sync response remains confirmed; its correlated report
     * resumes after the new connection's complete bootstrap state. */
}

void ABoxCloudService_SetReady(ABoxCloudService *service, int ready, uint32_t now)
{
    if (!service) return;
    if (!ready) {
        if (service->ready) ABoxCloudService_NetworkChanged(service);
        return;
    }
    if (service->ready) return;
    service->ready = 1U;
    service->bootstrap_stage = 1U;
    service->heartbeat_due = now;
    service->status_due = now;
}

int ABoxCloudService_Bootstrapping(const ABoxCloudService *service)
{
    return service && service->ready && service->bootstrap_stage != 0U;
}

int ABoxCloudService_QueueResponse(ABoxCloudService *service,
                                   const char *request_id, int sync_state)
{
    if (!service || service->response_pending ||
        (sync_state && service->sync_pending) ||
        !request_id || (sync_state && !request_id[0]) ||
        !copy_text(service->response_request_id,
                   sizeof(service->response_request_id), request_id)) return 0;
    service->response_pending = 1U;
    service->response_sync = sync_state ? 1U : 0U;
    if (sync_state && service->pending_sync_request_id &&
        strcmp(service->pending_sync_request_id, request_id) == 0)
        service->pending_sync_request_id[0] = '\0';
    return 1;
}

int ABoxCloudService_SetSyncStorage(ABoxCloudService *service, char *storage,
                                    unsigned capacity)
{
    if (!service || !storage || capacity < 65U || service->pending_sync_request_id)
        return 0;
    service->pending_sync_request_id = storage;
    storage[0] = '\0';
    return 1;
}

int ABoxCloudService_QueueSyncRequest(ABoxCloudService *service, const char *request_id)
{
    if (!service || !service->pending_sync_request_id ||
        service->pending_sync_request_id[0] || !request_id || !request_id[0])
        return 0;
    return copy_text(service->pending_sync_request_id,
                     65U, request_id);
}

void ABoxCloudService_CancelResponse(ABoxCloudService *service)
{
    if (!service) return;
    service->response_pending = 0U;
    service->response_sync = 0U;
    service->response_request_id[0] = '\0';
    if (service->in_flight == ABOX_CLOUD_JOB_RESPONSE) {
        service->in_flight = ABOX_CLOUD_JOB_NONE;
        service->operation = 0U;
    }
}

int ABoxCloudService_QueueStateReport(ABoxCloudService *service,
                                      const char *request_id, const char *reason)
{
    char id[sizeof(service->sync_request_id)];
    char why[sizeof(service->correlated_reason)];
    if (!service || !request_id || !request_id[0] || !reason || !reason[0] ||
        !copy_text(id, sizeof(id), request_id) ||
        !copy_text(why, sizeof(why), reason)) return 0;
    (void)copy_text(service->sync_request_id,
                    sizeof(service->sync_request_id), id);
    (void)copy_text(service->correlated_reason,
                    sizeof(service->correlated_reason), why);
    service->sync_pending = 1U;
    if (++service->correlated_serial == 0U) ++service->correlated_serial;
    return 1;
}

int ABoxCloudService_WakeState(ABoxCloudService *service, const char *reason)
{
    if (!service || !reason || !reason[0] ||
        !copy_text(service->event_reason, sizeof(service->event_reason), reason))
        return 0;
    service->event_pending = 1U;
    service->event_serial++;
    return 1;
}

int ABoxCloudService_Next(ABoxCloudService *service, uint32_t now,
                          ABoxCloudJob *job)
{
    if (!service || !job) return 0;
    memset(job, 0, sizeof(*job));
    if (!service->ready || service->in_flight != ABOX_CLOUD_JOB_NONE) return 0;
    job->generation = service->generation;
    job->state_index=service->state_index;
    if (service->response_pending && service->options.response_preempts_bootstrap) {
        job->kind = ABOX_CLOUD_JOB_RESPONSE;
        job->request_id = service->response_request_id;
    } else if (service->bootstrap_stage == 1U) job->kind = ABOX_CLOUD_JOB_MANIFEST;
    else if (service->bootstrap_stage == 2U) job->kind = ABOX_CLOUD_JOB_HEARTBEAT;
    else if (service->bootstrap_stage == 3U) {
        job->kind = ABOX_CLOUD_JOB_STATE;
        job->reason = service->ever_ready ? "reconnect" : "startup";
    } else if (service->product_state_pending) {
        job->kind=ABOX_CLOUD_JOB_PRODUCT_STATE;
    } else if (service->response_pending) {
        job->kind = ABOX_CLOUD_JOB_RESPONSE;
        job->request_id = service->response_request_id;
    } else if (service->sync_pending) {
        job->kind = ABOX_CLOUD_JOB_STATE;
        job->reason = service->correlated_reason[0] ?
            service->correlated_reason : "sync_state";
        job->request_id = service->sync_request_id;
    } else if (service->pending_sync_request_id && service->pending_sync_request_id[0]) {
        job->kind = ABOX_CLOUD_JOB_RESPONSE;
        job->request_id = service->pending_sync_request_id;
    } else if (service->event_pending) {
        job->kind = ABOX_CLOUD_JOB_STATE;
        job->reason = service->event_reason;
    } else if ((int32_t)(now - service->heartbeat_due) >= 0) {
        job->kind = ABOX_CLOUD_JOB_HEARTBEAT;
    } else if (service->options.status_period_ms &&
               (int32_t)(now - service->status_due) >= 0) {
        job->kind = ABOX_CLOUD_JOB_STATE;
        job->reason = "periodic";
    } else return 0;
    return 1;
}

int ABoxCloudService_BeginExternal(ABoxCloudService *service,
                                   uint64_t operation, uint32_t now)
{
    return ABoxCloudService_BeginExternalPriority(service, operation, now, 0);
}

int ABoxCloudService_BeginExternalPriority(ABoxCloudService *service,
    uint64_t operation, uint32_t now, int preempt_periodic)
{
    ABoxCloudJob job;
    if (!service || !operation || !service->ready || service->in_flight ||
        (ABoxCloudService_Next(service, now, &job) &&
         !(preempt_periodic && !service->bootstrap_stage &&
           (job.kind == ABOX_CLOUD_JOB_HEARTBEAT ||
            (job.kind == ABOX_CLOUD_JOB_STATE && job.reason &&
             strcmp(job.reason, "periodic") == 0))))) return 0;
    service->in_flight = ABOX_CLOUD_JOB_EXTERNAL;
    service->operation = operation;
    return 1;
}

int ABoxCloudService_Begin(ABoxCloudService *service, const ABoxCloudJob *job,
                           uint64_t operation, uint32_t now)
{
    ABoxCloudJob expected;
    if (!service || !job || !operation || service->in_flight != ABOX_CLOUD_JOB_NONE ||
        (job->kind == ABOX_CLOUD_JOB_RESPONSE && !service->response_pending) ||
        !ABoxCloudService_Next(service, now, &expected) ||
        job->kind != expected.kind || job->generation != expected.generation ||
        ((job->request_id || expected.request_id) &&
         (!job->request_id || !expected.request_id ||
          strcmp(job->request_id, expected.request_id) != 0)) ||
        ((job->reason || expected.reason) &&
         (!job->reason || !expected.reason ||
          strcmp(job->reason, expected.reason) != 0)))
        return 0;
    service->in_flight = job->kind;
    service->operation = operation;
    service->in_flight_state_count = job->kind == ABOX_CLOUD_JOB_STATE &&
        !service->bootstrap_stage && !service->sync_pending && !service->event_pending && service->periodic_state_count
        ? service->periodic_state_count : service->state_count;
    service->in_flight_event_serial =
        job->kind == ABOX_CLOUD_JOB_STATE && service->event_pending &&
        !service->sync_pending && service->bootstrap_stage == 0U
            ? service->event_serial : 0U;
    service->in_flight_correlated_serial =
        job->kind == ABOX_CLOUD_JOB_STATE && service->sync_pending &&
        service->bootstrap_stage == 0U ? service->correlated_serial : 0U;
    return 1;
}

ABoxCloudJobKind ABoxCloudService_Receipt(ABoxCloudService *service,
                                          uint32_t generation, uint64_t operation,
                                          int confirmed, uint32_t now)
{
    ABoxCloudJobKind kind;
    if (!service || !operation || generation != service->generation ||
        operation != service->operation || service->in_flight == ABOX_CLOUD_JOB_NONE)
        return ABOX_CLOUD_JOB_NONE;
    kind = service->in_flight;
    service->in_flight = ABOX_CLOUD_JOB_NONE;
    service->operation = 0U;
    if (!confirmed) return ABOX_CLOUD_JOB_NONE;
    if (kind==ABOX_CLOUD_JOB_PRODUCT_STATE) { service->product_state_pending=0U; return kind; }
    if (kind==ABOX_CLOUD_JOB_STATE) {
        if (service->state_index+1U<service->in_flight_state_count) { ++service->state_index; return ABOX_CLOUD_JOB_NONE; }
        service->state_index=0U;
    }

    if (kind == ABOX_CLOUD_JOB_MANIFEST && service->bootstrap_stage == 1U)
        service->bootstrap_stage = 2U;
    else if (kind == ABOX_CLOUD_JOB_HEARTBEAT && service->bootstrap_stage == 2U)
        service->bootstrap_stage = 3U;
    else if (kind == ABOX_CLOUD_JOB_STATE && service->bootstrap_stage == 3U) {
        service->bootstrap_stage = 0U;
        service->ever_ready = 1U;
        service->heartbeat_due = now + service->options.heartbeat_period_ms;
        service->status_due = now + service->options.status_period_ms;
    } else if (kind == ABOX_CLOUD_JOB_RESPONSE && service->response_pending) {
        service->response_pending = 0U;
        if (service->response_sync) {
            (void)copy_text(service->sync_request_id,
                            sizeof(service->sync_request_id),
                            service->response_request_id);
            (void)copy_text(service->correlated_reason,
                            sizeof(service->correlated_reason), "sync_state");
            service->sync_pending = 1U;
            if (++service->correlated_serial == 0U) ++service->correlated_serial;
            service->response_sync = 0U;
        }
    } else if (kind == ABOX_CLOUD_JOB_STATE && service->sync_pending) {
        if (service->correlated_serial == service->in_flight_correlated_serial)
            service->sync_pending = 0U;
        service->status_due = now + service->options.status_period_ms;
    } else if (kind == ABOX_CLOUD_JOB_STATE && service->event_pending) {
        if (service->event_serial == service->in_flight_event_serial)
            service->event_pending = 0U;
        service->status_due = now + service->options.status_period_ms;
    } else if (kind == ABOX_CLOUD_JOB_HEARTBEAT)
        service->heartbeat_due = now + service->options.heartbeat_period_ms;
    else if (kind == ABOX_CLOUD_JOB_STATE)
        service->status_due = now + service->options.status_period_ms;
    return kind;
}

ABoxCloudPollResult ABoxCloudService_Poll(ABoxCloudService *service,
    const ABoxCloudServicePort *port, uint32_t now)
{
    ABoxCloudJob job;
    uint64_t operation = 0U;
    int has_job, external;
    if (!service || !port || !port->ready || !port->publish_active ||
        !port->receipt || !port->publish) return ABOX_CLOUD_POLL_BLOCKED;
    if (service->in_flight) {
        ABoxMqttReceiptStatus result = port->receipt(port->context, service->operation);
        if (result == ABOX_MQTT_RECEIPT_CONFIRMED) {
            ABoxCloudJobKind kind = ABoxCloudService_Receipt(service,
                service->generation, service->operation, 1, now);
            if (kind && port->completed) port->completed(port->context, kind);
        } else if (result == ABOX_MQTT_RECEIPT_PENDING && port->publish_active(port->context))
            return ABOX_CLOUD_POLL_BLOCKED;
        else {
            (void)ABoxCloudService_Receipt(service, service->generation,
                                          service->operation, 0, now);
            if (port->failed) port->failed(port->context);
            return ABOX_CLOUD_POLL_BLOCKED;
        }
    }
    if (!service->ready || !port->ready(port->context) || port->publish_active(port->context))
        return ABOX_CLOUD_POLL_BLOCKED;
    has_job = ABoxCloudService_Next(service, now, &job);
    if (port->external && (!has_job ||
        (port->external_preempts_periodic && !service->bootstrap_stage &&
         (job.kind == ABOX_CLOUD_JOB_HEARTBEAT ||
          (job.kind == ABOX_CLOUD_JOB_STATE && job.reason && !strcmp(job.reason, "periodic")))))) {
        external = port->external(port->context, &operation);
        if (external > 0) {
            if (ABoxCloudService_BeginExternalPriority(service, operation, now,
                                                       port->external_preempts_periodic))
                return ABOX_CLOUD_POLL_SUBMITTED;
            if (port->failed) port->failed(port->context);
            return ABOX_CLOUD_POLL_BLOCKED;
        }
        if (external < 0) return ABOX_CLOUD_POLL_BLOCKED;
    }
    if (!has_job) return ABOX_CLOUD_POLL_IDLE;
    operation = 0U;
    if (!port->publish(port->context, &job, &operation)) return ABOX_CLOUD_POLL_BLOCKED;
    if (ABoxCloudService_Begin(service, &job, operation, now)) return ABOX_CLOUD_POLL_SUBMITTED;
    if (port->failed) port->failed(port->context);
    return ABOX_CLOUD_POLL_BLOCKED;
}
