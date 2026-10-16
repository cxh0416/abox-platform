#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_boot_v2_services.h"
#include "product_config_stub.h"

/* Compile each product's real OTA adapter with failed dependency bindings. */
#include PRODUCT_OTA_SOURCE

uint8_t __app_image_end;
static int binding_ok, layout_ok, app_ok, initialized;
static unsigned app_attempts, logs;
static int app_busy;
static const char* app_phase = "WAIT_MODEM";
static unsigned ota_events_count, tasks;
#ifdef SWEEPER_OTA_SERVICE_H
static unsigned replay_events_count;
#endif
static ABoxEc800At* bound_at;
static void original_event(ABoxEc800Event e, const uint8_t* p, uint16_t n, void* u)
{ (void)e; (void)p; (void)n; (void)u; ++ota_events_count; }
static ABoxMqttEc800Bridge bridge_stub;
static ProductConfigStub config_stub = { "https://ota.example" };
void Test_Log(void) { ++logs; }
void Error_Handler(void) { assert(!"OTA failure must not halt MCU startup"); }
const char *LockCtrl_AppFwVersion(void) { return "meal-delivery-vehicle-test"; }
const char *Vcu_AppFwVersion(void) { return "sweeper-vcu-test"; }
const char *Patrol_AppFwVersion(void) { return "patrol-chassis-test"; }
const ProductConfigStub *VcuConfig_Get(void) { return &config_stub; }
const ProductConfigStub *PatrolConfig_Get(void) { return &config_stub; }
uint8_t MqttRuntime_ActiveClient(void) { return 1U; }
ABoxMqttEc800Bridge *MQTT_Core_Bridge(void) { return &bridge_stub; }
int ABoxBootV2Ec800Adapter_BindServices(ABoxBootV2Ec800Adapter *a,
    ABoxEc800At *at, ABoxMqttEc800Bridge *b, ABoxBootV2AppPort *p)
{ (void)b; (void)p; a->at = at; bound_at = at; return binding_ok; }
int ABoxBootV2_StateBindLayout(const ABoxBootV2Layout *l)
{ (void)l; return layout_ok; }
int ABoxBootV2App_Init(const ABoxBootV2AppPort *p, const ABoxBootV2AppConfig *c)
{
    initialized = 0;
    if (!p || !c) return 0;
    ++app_attempts; initialized = app_ok;
    if (app_ok) assert(ABoxEc800At_Register(bound_at, ABOX_EC800_OWNER_OTA, original_event, NULL));
    return app_ok;
}
int ABoxBootV2App_IsInitialized(void) { return initialized; }
int ABoxBootV2App_BeginProvisioning(void) { return initialized; }
void ABoxBootV2App_Task(void) { ++tasks; }
int ABoxBootV2App_IsProvisioned(void) { return initialized; }
int ABoxBootV2App_IsReady(void) { return initialized; }
int ABoxBootV2App_IsBusy(void) { return app_busy; }
const char *ABoxBootV2App_Phase(void) { return initialized ? app_phase : "INIT_FAILED"; }
const char *ABoxBootV2App_TargetVersion(void) { return ""; }
uint32_t ABoxBootV2App_LastError(void) { return initialized ? 0U : ABOX_BOOT_V2_APP_ERROR_STATE; }
int ABoxBootV2App_Start(const ABoxBootV2AppRequest *r)
{ (void)r; assert(initialized); return 1; }
int ABoxBootV2App_TakeInstallReady(void) { return 0; }
int ABoxBootV2App_TakeFailure(uint32_t *e) { if(e) *e = ABoxBootV2App_LastError(); return 0; }
int ABoxBootV2_AcknowledgeRollback(void) { return 1; }

static void check_disabled(void)
{
    uint32_t error;
    assert(!OtaService_IsInitialized());
    OtaService_Task();
    assert(!OtaService_BeginProvisioning() && !OtaService_IsReady());
    assert(!OtaService_IsProvisioned() && !OtaService_IsBusy());
    assert(!OtaService_Start((const void *)(uintptr_t)1U));
    assert(!strcmp(OtaService_Phase(), "INIT_FAILED"));
    assert(OtaService_LastError() == ABOX_BOOT_V2_APP_ERROR_STATE);
    assert(!OtaService_TakeInstallReady() && !OtaService_TakeFailure(&error));
}
#ifdef SWEEPER_OTA_SERVICE_H
static void replay_event(ABoxEc800Event e, const uint8_t* p, uint16_t n, void* u)
{ (void)e; (void)p; (void)n; (void)u; ++replay_events_count; }
static void check_replay_workspace(ABoxEc800At* at)
{
    /* Failure to attach the optional journal must preserve the OTA binding. */
    at->notifying = 1;
    assert(!OtaService_SetReplayEvents(replay_event));
    assert(OtaService_IsInitialized() && OtaService_IsReady() && OtaService_BeginProvisioning());
    assert(at->handlers[0].callback == original_event);
    at->notifying = 0;
    assert(OtaService_SetReplayEvents(replay_event));
    app_busy = 1; assert(!OtaService_BorrowReplayWorkspace()); app_busy = 0;
    app_phase = "PROVISIONING"; assert(!OtaService_BorrowReplayWorkspace()); app_phase = "IDLE";
    /* A queued OTA command also owns the buffer even if the state is idle. */
    at->queue[0].used = 1; at->queue[0].owner = ABOX_EC800_OWNER_OTA;
    assert(!OtaService_BorrowReplayWorkspace()); at->queue[0].used = 0;
    uint8_t* buffer = OtaService_BorrowReplayWorkspace();
    assert(buffer && buffer == g_ota_transfer_buffer && !OtaService_IsBusy());
    memset(buffer, 0xA5, ABOX_BOOT_V2_APP_RAW_CHUNK);
    assert(!OtaService_BorrowReplayWorkspace() && !OtaService_BeginProvisioning());
    assert(!OtaService_Start((const void*)(uintptr_t)1U));
    unsigned before = tasks; OtaService_Task(); assert(tasks == before);
    assert(!OtaService_SetReplayEvents(replay_event));
    shared_ufs_event(ABOX_EC800_EVENT_RAW, (const uint8_t*)"OK", 2, NULL);
    assert(replay_events_count == 1 && !ota_events_count);
    /* The real AT core broadcasts reset to both users through the one slot. */
    ABoxEc800At_OnModemReset(at);
    assert(replay_events_count == 2 && ota_events_count == 1);
    OtaService_ReleaseReplayWorkspace();
    assert(!OtaService_IsBusy() && OtaService_BeginProvisioning());
    CloudProtocolRequest_t request = {0};
    strcpy(request.ota_url, "https://ota.example/firmware.bin");
    strcpy(request.ota_version, "sweeper-vcu-test");
    assert(OtaService_Start(&request));
    OtaService_Task(); assert(tasks == before + 1);
    shared_ufs_event(ABOX_EC800_EVENT_LINE, (const uint8_t*)"OK", 2, NULL);
    assert(replay_events_count == 2 && ota_events_count == 2);
    ABoxEc800At_OnModemReset(at);
    assert(replay_events_count == 3 && ota_events_count == 3);
    for (unsigned i = 0; i < sizeof(g_ota_transfer_buffer); ++i) assert(buffer[i] == 0xA5);
}
#endif
int main(void)
{
    ABoxEc800At at = {0};
    binding_ok = 0; layout_ok = app_ok = 1;
    OtaService_Init(&at); check_disabled(); assert(app_attempts == 0U && logs);
    binding_ok = 1; layout_ok = 0;
    OtaService_Init(&at); check_disabled(); assert(app_attempts == 0U);
    layout_ok = 1; app_ok = 0;
    OtaService_Init(&at); check_disabled(); assert(app_attempts == 1U);
    app_ok = 1;
    OtaService_Init(&at); assert(OtaService_IsInitialized());
#ifdef SWEEPER_OTA_SERVICE_H
    check_replay_workspace(&at);
#endif
    binding_ok = 0;
    OtaService_Init(&at); check_disabled(); assert(app_attempts == 2U);
    return 0;
}
