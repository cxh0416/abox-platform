#include <assert.h>
#include <string.h>

#include "abox_cloud_service.h"

static ABoxCloudJob next(ABoxCloudService *service, uint32_t now,
                         ABoxCloudJobKind kind)
{
    ABoxCloudJob job;
    assert(ABoxCloudService_Next(service, now, &job));
    assert(job.kind == kind);
    return job;
}

static void confirm(ABoxCloudService *service, ABoxCloudJob *job,
                    uint64_t operation, uint32_t now)
{
    assert(ABoxCloudService_Begin(service, job, operation, now));
    assert(ABoxCloudService_Receipt(service, job->generation, operation,
                                    1, now) == job->kind);
}

typedef struct {
    uint64_t operation, queried;
    ABoxMqttReceiptStatus receipt;
    ABoxCloudJobKind published, completed;
    int active, failures;
} PollFixture;
static int poll_ready(void *c) { (void)c; return 1; }
static int poll_active(void *c) { return ((PollFixture *)c)->active; }
static ABoxMqttReceiptStatus poll_receipt(void *c, uint64_t op) {
    PollFixture *f = c; f->queried = op; return f->receipt;
}
static int poll_publish(void *c, const ABoxCloudJob *job, uint64_t *op) {
    PollFixture *f = c; f->published = job->kind; *op = ++f->operation;
    f->active = 1; return 1;
}
static void poll_completed(void *c, ABoxCloudJobKind kind) {
    ((PollFixture *)c)->completed = kind;
}
static void poll_failed(void *c) { ++((PollFixture *)c)->failures; }
static void test_poll(void) {
    ABoxCloudService s;
    ABoxCloudServiceOptions options = {1000U, 3000U, 1U};
    PollFixture f = {0};
    ABoxCloudServicePort port = {&f, poll_ready, poll_active, poll_receipt,
        poll_publish, 0, poll_completed, poll_failed, 0};
    assert(ABoxCloudService_Init(&s, &options, 0));
    ABoxCloudService_SetReady(&s, 1, 0);
    assert(ABoxCloudService_QueueResponse(&s, "sync", 1));
    assert(ABoxCloudService_Poll(&s, &port, 0) == ABOX_CLOUD_POLL_SUBMITTED);
    assert(f.published == ABOX_CLOUD_JOB_RESPONSE);
    f.receipt = ABOX_MQTT_RECEIPT_PENDING;
    assert(ABoxCloudService_Poll(&s, &port, 1) == ABOX_CLOUD_POLL_BLOCKED);
    assert(f.queried == 1 && !s.sync_pending);
    f.active = 0; f.receipt = ABOX_MQTT_RECEIPT_CONFIRMED;
    assert(ABoxCloudService_Poll(&s, &port, 2) == ABOX_CLOUD_POLL_SUBMITTED);
    assert(f.completed == ABOX_CLOUD_JOB_RESPONSE && s.sync_pending);
    assert(f.published == ABOX_CLOUD_JOB_MANIFEST);
    f.active = 0; f.receipt = ABOX_MQTT_RECEIPT_PENDING;
    assert(ABoxCloudService_Poll(&s, &port, 3) == ABOX_CLOUD_POLL_BLOCKED);
    assert(f.failures == 1 && !s.in_flight && s.bootstrap_stage == 1);
    uint32_t generation = s.generation;
    ABoxCloudService_NetworkChanged(&s);
    assert(ABoxCloudService_Receipt(&s, generation, 2, 1, 4) == ABOX_CLOUD_JOB_NONE);
    assert(ABoxCloudService_Poll(&s, 0, 4) == ABOX_CLOUD_POLL_BLOCKED);
}

static void test_separate_periodic_owner(void)
{
    ABoxCloudService s;
    ABoxCloudServiceOptions options={5000,5000,0};
    ABoxCloudJob job;
    uint64_t op=200;
    assert(ABoxCloudService_Init(&s,&options,0));
    ABoxCloudService_SetStateCount(&s,2);
    ABoxCloudService_SetPeriodicStateCount(&s,1);
    ABoxCloudService_SetReady(&s,1,0);
    job=next(&s,0,ABOX_CLOUD_JOB_MANIFEST); confirm(&s,&job,++op,0);
    job=next(&s,0,ABOX_CLOUD_JOB_HEARTBEAT); confirm(&s,&job,++op,0);
    job=next(&s,0,ABOX_CLOUD_JOB_STATE); assert(job.state_index==0);
    assert(ABoxCloudService_Begin(&s,&job,++op,0));
    assert(ABoxCloudService_Receipt(&s,s.generation,op,1,0)==ABOX_CLOUD_JOB_NONE);
    job=next(&s,0,ABOX_CLOUD_JOB_STATE); assert(job.state_index==1); confirm(&s,&job,++op,0);
    job=next(&s,5000,ABOX_CLOUD_JOB_HEARTBEAT); confirm(&s,&job,++op,5000);
    job=next(&s,5000,ABOX_CLOUD_JOB_STATE); assert(job.state_index==0);
    assert(ABoxCloudService_Begin(&s,&job,++op,5000));
    /* A newly queued full sync cannot turn this one-state periodic receipt
     * into its first state, or erase the new correlation. */
    assert(ABoxCloudService_QueueStateReport(&s,"new-sync","sync_state"));
    assert(ABoxCloudService_Receipt(&s,s.generation,op,1,5000)==ABOX_CLOUD_JOB_STATE);
    job=next(&s,5000,ABOX_CLOUD_JOB_STATE);
    assert(job.state_index==0 && !strcmp(job.request_id,"new-sync"));
    assert(ABoxCloudService_Begin(&s,&job,++op,5000));
    assert(ABoxCloudService_Receipt(&s,s.generation,op,1,5000)==ABOX_CLOUD_JOB_NONE);
    job=next(&s,5000,ABOX_CLOUD_JOB_STATE);
    assert(job.state_index==1 && !strcmp(job.request_id,"new-sync")); confirm(&s,&job,++op,5000);
    assert(!ABoxCloudService_Next(&s,5000,&job));
    assert(ABoxCloudService_WakeState(&s,"local_changed"));
    job=next(&s,5000,ABOX_CLOUD_JOB_STATE); assert(job.state_index==0);
    assert(ABoxCloudService_Begin(&s,&job,++op,5000));
    assert(ABoxCloudService_Receipt(&s,s.generation,op,1,5000)==ABOX_CLOUD_JOB_NONE);
    job=next(&s,5000,ABOX_CLOUD_JOB_STATE); assert(job.state_index==1); confirm(&s,&job,++op,5000);
    job=next(&s,10000,ABOX_CLOUD_JOB_HEARTBEAT); confirm(&s,&job,++op,10000);
    job=next(&s,10000,ABOX_CLOUD_JOB_STATE); assert(job.state_index==0); confirm(&s,&job,++op,10000);
    assert(!ABoxCloudService_Next(&s,10000,&job));
}

int main(void)
{
    test_poll();
    test_separate_periodic_owner();
    ABoxCloudBootstrap compact;
    ABoxCloudTxTracker tracker;
    ABoxCloudService service;
    ABoxCloudServiceOptions options = {1000U, 3000U, 0U};
    ABoxCloudJob job;
    uint32_t generation;
    uint8_t tracked_kind = 0U;

    assert(!ABoxCloudTxTracker_Init(0));
    assert(ABoxCloudTxTracker_Init(&tracker));
    assert(sizeof(tracker) <= 16U);
    assert(!ABoxCloudTxTracker_Begin(&tracker, 0U, 1U));
    assert(!ABoxCloudTxTracker_Begin(&tracker, 1U, 0U));
    assert(ABoxCloudTxTracker_Begin(&tracker, 2U, 41U));
    assert(!ABoxCloudTxTracker_Begin(&tracker, 3U, 42U));
    generation = tracker.generation;
    assert(ABoxCloudTxTracker_Receipt(&tracker, generation, 40U, 1,
                                      &tracked_kind) == ABOX_CLOUD_TX_IGNORED);
    assert(ABoxCloudTxTracker_Receipt(&tracker, generation, 41U, 0,
                                      &tracked_kind) == ABOX_CLOUD_TX_FAILED);
    assert(tracked_kind == 2U && !tracker.kind);
    assert(ABoxCloudTxTracker_Begin(&tracker, 3U, 42U));
    ABoxCloudTxTracker_SetReady(&tracker, 1);
    ABoxCloudTxTracker_SetReady(&tracker, 0);
    assert(!tracker.kind);
    assert(ABoxCloudTxTracker_Receipt(&tracker, generation, 42U, 1,
                                      &tracked_kind) == ABOX_CLOUD_TX_IGNORED);
    ABoxCloudTxTracker_SetReady(&tracker, 1);
    assert(ABoxCloudTxTracker_Begin(&tracker, 3U, 42U));
    ABoxCloudTxTracker_NetworkChanged(&tracker);
    assert(ABoxCloudTxTracker_Begin(&tracker, 4U, 43U));
    assert(ABoxCloudTxTracker_Receipt(&tracker, generation, 42U, 1,
                                      &tracked_kind) == ABOX_CLOUD_TX_IGNORED);
    assert(ABoxCloudTxTracker_Receipt(&tracker, tracker.generation, 43U, 1,
                                      &tracked_kind) == ABOX_CLOUD_TX_CONFIRMED);
    assert(tracked_kind == 4U && !tracker.kind);

    assert(!ABoxCloudBootstrap_Init(0));
    assert(ABoxCloudBootstrap_Init(&compact));
    assert(sizeof(compact) < 32U);
    assert(!ABoxCloudBootstrap_Next(&compact, &job));
    ABoxCloudBootstrap_SetReady(&compact, 1);
    assert(ABoxCloudBootstrap_Next(&compact, &job));
    assert(job.kind == ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 1U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 2U, 1) == ABOX_CLOUD_JOB_NONE);
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 1U, 0) == ABOX_CLOUD_JOB_NONE);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 3U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 3U, 1) == ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_HEARTBEAT);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 4U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 4U, 1) == ABOX_CLOUD_JOB_HEARTBEAT);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "startup") == 0);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 5U));
    generation = job.generation;
    ABoxCloudBootstrap_NetworkChanged(&compact);
    assert(ABoxCloudBootstrap_Receipt(&compact, generation, 5U, 1) == ABOX_CLOUD_JOB_NONE);
    ABoxCloudBootstrap_SetReady(&compact, 1);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 6U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 6U, 1) == ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_HEARTBEAT);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 7U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 7U, 1) == ABOX_CLOUD_JOB_HEARTBEAT);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "startup") == 0);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 8U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 8U, 1) == ABOX_CLOUD_JOB_STATE);
    ABoxCloudBootstrap_SetReady(&compact, 0);
    ABoxCloudBootstrap_SetReady(&compact, 1);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 9U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 9U, 1) == ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_HEARTBEAT);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 10U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 10U, 1) == ABOX_CLOUD_JOB_HEARTBEAT);
    assert(ABoxCloudBootstrap_Next(&compact, &job) && job.kind == ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "reconnect") == 0);
    assert(ABoxCloudBootstrap_Begin(&compact, &job, 11U));
    assert(ABoxCloudBootstrap_Receipt(&compact, job.generation, 11U, 1) == ABOX_CLOUD_JOB_STATE);
    assert(!ABoxCloudBootstrap_Active(&compact));

    assert(!ABoxCloudService_Init(&service, 0, 0U));
    assert(ABoxCloudService_Init(&service, &options, 100U));
    assert(!ABoxCloudService_Next(&service, 100U, &job));
    assert(ABoxCloudService_QueueResponse(&service, "req-1", 1));
    assert(!ABoxCloudService_QueueResponse(&service, "req-2", 0));
    ABoxCloudService_SetReady(&service, 1, 200U);
    job = next(&service, 200U, ABOX_CLOUD_JOB_MANIFEST);
    assert(ABoxCloudService_Begin(&service, &job, 10U, 200U));
    assert(!ABoxCloudService_Next(&service, 200U, &job));
    assert(ABoxCloudService_Receipt(&service, service.generation, 9U, 1, 201U)
           == ABOX_CLOUD_JOB_NONE);
    assert(ABoxCloudService_Receipt(&service, service.generation, 10U, 1, 201U)
           == ABOX_CLOUD_JOB_MANIFEST);
    job = next(&service, 201U, ABOX_CLOUD_JOB_HEARTBEAT);
    confirm(&service, &job, 11U, 201U);
    job = next(&service, 202U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "startup") == 0);
    confirm(&service, &job, 12U, 202U);

    job = next(&service, 203U, ABOX_CLOUD_JOB_RESPONSE);
    assert(strcmp(job.request_id, "req-1") == 0);
    confirm(&service, &job, 13U, 203U);
    job = next(&service, 204U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "sync_state") == 0);
    assert(strcmp(job.request_id, "req-1") == 0);
    assert(ABoxCloudService_Begin(&service, &job, 14U, 204U));
    generation = service.generation;
    ABoxCloudService_SetReady(&service, 0, 205U);
    assert(service.generation != generation);
    assert(ABoxCloudService_Receipt(&service, generation, 14U, 1, 206U)
           == ABOX_CLOUD_JOB_NONE);
    ABoxCloudService_SetReady(&service, 1, 207U);
    job = next(&service, 207U, ABOX_CLOUD_JOB_MANIFEST);
    confirm(&service, &job, 15U, 207U);
    job = next(&service, 207U, ABOX_CLOUD_JOB_HEARTBEAT);
    confirm(&service, &job, 16U, 207U);
    job = next(&service, 207U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "reconnect") == 0);
    confirm(&service, &job, 17U, 207U);
    job = next(&service, 208U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "sync_state") == 0);
    confirm(&service, &job, 18U, 208U);
    assert(!ABoxCloudService_Next(&service, 209U, &job));

    assert(ABoxCloudService_WakeState(&service, "power_change"));
    job = next(&service, 210U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "power_change") == 0);
    {
        ABoxCloudJob wrong = job;
        wrong.reason = "fabricated";
        assert(!ABoxCloudService_Begin(&service, &wrong, 19U, 210U));
    }
    assert(ABoxCloudService_Begin(&service, &job, 19U, 210U));
    assert(ABoxCloudService_Receipt(&service, job.generation, 19U, 0, 211U)
           == ABOX_CLOUD_JOB_NONE);
    job = next(&service, 212U, ABOX_CLOUD_JOB_STATE);
    confirm(&service, &job, 20U, 212U);
    assert(ABoxCloudService_WakeState(&service, "first"));
    job = next(&service, 213U, ABOX_CLOUD_JOB_STATE);
    assert(ABoxCloudService_Begin(&service, &job, 22U, 213U));
    assert(ABoxCloudService_WakeState(&service, "second"));
    assert(ABoxCloudService_Receipt(&service, job.generation, 22U, 1, 214U)
           == ABOX_CLOUD_JOB_STATE);
    job = next(&service, 214U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.reason, "second") == 0);
    confirm(&service, &job, 23U, 214U);
    assert(ABoxCloudService_QueueStateReport(&service, "report-1", "command_result"));
    job = next(&service, 215U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.request_id, "report-1") == 0);
    assert(strcmp(job.reason, "command_result") == 0);
    assert(ABoxCloudService_Begin(&service, &job, 24U, 215U));
    assert(ABoxCloudService_QueueStateReport(&service, "report-2", "sync_state"));
    assert(ABoxCloudService_Receipt(&service, job.generation, 24U, 1, 216U)
           == ABOX_CLOUD_JOB_STATE);
    job = next(&service, 216U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.request_id, "report-2") == 0);
    assert(strcmp(job.reason, "sync_state") == 0);
    confirm(&service, &job, 25U, 216U);
    job = next(&service, 1207U, ABOX_CLOUD_JOB_HEARTBEAT);
    confirm(&service, &job, 21U, 1207U);
    assert(!ABoxCloudService_Next(&service, 1208U, &job));
    options.status_period_ms = 0U;
    assert(ABoxCloudService_Init(&service, &options, 0U));
    ABoxCloudService_SetReady(&service, 1, 0U);
    job = next(&service, 0U, ABOX_CLOUD_JOB_MANIFEST);
    confirm(&service, &job, 31U, 0U);
    job = next(&service, 0U, ABOX_CLOUD_JOB_HEARTBEAT);
    confirm(&service, &job, 32U, 0U);
    job = next(&service, 0U, ABOX_CLOUD_JOB_STATE);
    confirm(&service, &job, 33U, 0U);
    assert(!ABoxCloudService_Next(&service, 999U, &job));
    job = next(&service, 1000U, ABOX_CLOUD_JOB_HEARTBEAT);
    confirm(&service, &job, 34U, 1000U);
    assert(!ABoxCloudService_Next(&service, 1500U, &job));

    options.response_preempts_bootstrap = 1U;
    assert(ABoxCloudService_Init(&service, &options, 0U));
    assert(ABoxCloudService_QueueStateReport(&service, "sync-1", "requested"));
    assert(ABoxCloudService_QueueResponse(&service, "sync-1", 0));
    ABoxCloudService_SetReady(&service, 1, 10U);
    job = next(&service, 10U, ABOX_CLOUD_JOB_RESPONSE);
    assert(strcmp(job.request_id, "sync-1") == 0);
    assert(ABoxCloudService_Begin(&service, &job, 100U, 10U));
    ABoxCloudService_CancelResponse(&service);
    assert(ABoxCloudService_Receipt(&service, job.generation, 100U, 1, 11U)
           == ABOX_CLOUD_JOB_NONE);
    assert(ABoxCloudService_QueueResponse(&service, "req-2", 0));
    job = next(&service, 12U, ABOX_CLOUD_JOB_RESPONSE);
    assert(strcmp(job.request_id, "req-2") == 0);
    confirm(&service, &job, 101U, 12U);
    job = next(&service, 12U, ABOX_CLOUD_JOB_MANIFEST);
    confirm(&service, &job, 102U, 12U);
    job = next(&service, 12U, ABOX_CLOUD_JOB_HEARTBEAT);
    confirm(&service, &job, 103U, 12U);
    job = next(&service, 12U, ABOX_CLOUD_JOB_STATE);
    confirm(&service, &job, 104U, 12U);
    job = next(&service, 12U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.request_id, "sync-1") == 0);
    confirm(&service, &job, 105U, 12U);
    char deferred_storage[65];
    assert(!ABoxCloudService_QueueSyncRequest(&service, "unbound"));
    assert(!ABoxCloudService_SetSyncStorage(&service, deferred_storage, 64U));
    assert(ABoxCloudService_SetSyncStorage(&service, deferred_storage, sizeof(deferred_storage)));
    /* A sync received during another response survives without replacing it. */
    assert(ABoxCloudService_QueueResponse(&service, "info", 0));
    assert(ABoxCloudService_QueueSyncRequest(&service, "deferred"));
    assert(!ABoxCloudService_QueueSyncRequest(&service, "second"));
    job = next(&service, 13U, ABOX_CLOUD_JOB_RESPONSE);
    assert(strcmp(job.request_id, "info") == 0);
    confirm(&service, &job, 106U, 13U);
    job = next(&service, 14U, ABOX_CLOUD_JOB_RESPONSE);
    assert(strcmp(job.request_id, "deferred") == 0);
    assert(!ABoxCloudService_Begin(&service, &job, 107U, 14U));
    assert(ABoxCloudService_QueueResponse(&service, job.request_id, 1));
    assert(!service.pending_sync_request_id[0]);
    job = next(&service, 14U, ABOX_CLOUD_JOB_RESPONSE);
    confirm(&service, &job, 107U, 14U);
    job = next(&service, 15U, ABOX_CLOUD_JOB_STATE);
    assert(strcmp(job.request_id, "deferred") == 0);
    confirm(&service, &job, 108U, 15U);
    assert(ABoxCloudService_BeginExternal(&service, 109U, 16U));
    assert(!ABoxCloudService_BeginExternal(&service, 110U, 16U));
    assert(ABoxCloudService_Receipt(&service, service.generation, 110U, 1, 17U)
           == ABOX_CLOUD_JOB_NONE);
    assert(ABoxCloudService_Receipt(&service, service.generation, 109U, 1, 17U)
           == ABOX_CLOUD_JOB_EXTERNAL);
    assert(!ABoxCloudService_BeginExternal(&service, 111U, 2000U));
    assert(ABoxCloudService_BeginExternalPriority(&service, 111U, 2000U, 1));
    assert(ABoxCloudService_Receipt(&service, service.generation, 111U, 1, 2001U)
           == ABOX_CLOUD_JOB_EXTERNAL);
    assert(ABoxCloudService_QueueResponse(&service, "urgent", 0));
    assert(!ABoxCloudService_BeginExternalPriority(&service, 112U, 2001U, 1));
    return 0;
}
