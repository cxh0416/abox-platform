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
{ (void)a; (void)at; (void)b; (void)p; return binding_ok; }
int ABoxBootV2_StateBindLayout(const ABoxBootV2Layout *l)
{ (void)l; return layout_ok; }
int ABoxBootV2App_Init(const ABoxBootV2AppPort *p, const ABoxBootV2AppConfig *c)
{
    initialized = 0;
    if (!p || !c) return 0;
    ++app_attempts; initialized = app_ok;
    return app_ok;
}
int ABoxBootV2App_IsInitialized(void) { return initialized; }
int ABoxBootV2App_BeginProvisioning(void) { return initialized; }
void ABoxBootV2App_Task(void) {}
int ABoxBootV2App_IsProvisioned(void) { return initialized; }
int ABoxBootV2App_IsReady(void) { return initialized; }
int ABoxBootV2App_IsBusy(void) { return 0; }
const char *ABoxBootV2App_Phase(void) { return initialized ? "WAIT_MODEM" : "INIT_FAILED"; }
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
    binding_ok = 0;
    OtaService_Init(&at); check_disabled(); assert(app_attempts == 2U);
    return 0;
}
