/* Compile the actual product adapter against real Platform AT/Runtime. */
#include PRODUCT_RUNTIME_SOURCE
#include <assert.h>

static ABoxMqttTrialState trial_state;
static uint8_t readback_failed;
static uint8_t online, paused, security;
static unsigned resets;
static uint32_t now;
uint32_t HAL_GetTick(void) { return now; }
void MQTT_Core_SetPaused(uint8_t v) { paused = v; }
void MQTT_Core_SetSecurityReady(uint8_t v) { security = v; }
uint8_t MQTT_Core_ApplyRuntimeConfig(const ABoxMqttConfig *c) { return c && c->host; }
void MQTT_Core_Task(void) { }
void MQTT_Core_Reset(void) { ++resets; online = 0; }
int MQTT_Core_Stop(uint32_t t) { (void)t; return 1; }
uint8_t MQTT_Core_IsReady(void) { return online; }
uint8_t MQTT_Core_IsConnected(void) { return online; }
uint8_t OtaService_IsProvisioned(void) { return 1; }
uint8_t OtaService_IsBusy(void) { return 0; }
uint8_t TimeBase_IsValid(void) { return 1; }
void TimeBase_UpdateFromCCLK(const char *l) { (void)l; }
void CloudService_NetworkChanged(void) { }
void CloudTx_NetworkChanged(void) { }
void RemoteService_RevokeForNetworkChange(void) { }
ABoxMqttTrialState CloudService_EnrollmentTrialState(void) { return trial_state; }
ABoxMqttTrialState MQTT_App_MqttTrialState(void) { return trial_state; }
ABoxMqttTrialState MqttTrialManager_State(void) { return trial_state; }
uint8_t MQTT_App_RuntimeRecoveryAllowed(void) { return !readback_failed && trial_state!=ABOX_MQTT_TRIAL_RESTORE_FAILED; }
uint8_t CloudService_RuntimeRecoveryAllowed(void) { return MQTT_App_RuntimeRecoveryAllowed(); }
uint8_t MqttTrialManager_RuntimeRecoveryAllowed(void) { return MQTT_App_RuntimeRecoveryAllowed(); }
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState s, uint32_t t) { (void)s; (void)t; }
static uint32_t tick(void *u) { (void)u; return now; }
static int write_at(void *u, const uint8_t *d, uint16_t n, uint32_t t)
{ (void)u; (void)d; (void)n; (void)t; return 1; }
#ifdef PRODUCT_SWEEPER
#define INIT SweeperMqttRuntime_Init
#define TASK SweeperMqttRuntime_Task
#define NEEDS SweeperMqttRuntime_NeedsModemRecovery
#else
#define INIT MqttRuntime_Init
#define TASK MqttRuntime_Task
#define NEEDS MqttRuntime_NeedsModemRecovery
#endif
int main(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort p = {.tick_ms=tick, .write=write_at};
    ABoxMqttConfig c = {.host="fixture.example", .port=1883, .username="u", .password="p"};
    assert(ABoxEc800At_Init(&at, &p));
    assert(INIT(&at, &c, 0));
    TASK(1); online=1; TASK(2); TASK(3);
    assert(ABoxMqttRuntime_IsReady(&g_runtime));
    online=0;
    g_runtime.state=ABOX_MQTT_RUNTIME_CONNECT;
    g_runtime.started=0;
    TASK(60001);
    assert(NEEDS());
    ABoxEc800At_AbortAll(&at, ABOX_EC800_RESULT_ERROR);
    ABoxEc800At_Feed(&at, (const uint8_t *)"RDY\r\n", 5);
    assert(!NEEDS() && !g_runtime.blocked && !g_runtime.waiting);
    TASK(60002); online=1; TASK(60003); TASK(60004);
    assert(ABoxMqttRuntime_IsReady(&g_runtime));
    ABoxMqttConfig staged=c; staged.host="staged.example";
    assert(ABoxMqttRuntime_Stage(&g_runtime,&staged));
    ABoxEc800At_OnModemReset(&at);
    assert(g_runtime.candidate_staged && g_runtime.state==ABOX_MQTT_RUNTIME_FAILED);
    assert(paused && !security && !NEEDS());
    assert(!strcmp(ABoxMqttRuntime_ActiveConfig(&g_runtime)->host,"fixture.example"));
    assert(ABoxMqttRuntime_Activate(&g_runtime,60005));
    TASK(60006); TASK(60007); online=1; TASK(60008); TASK(60009);
    assert(ABoxMqttRuntime_IsReady(&g_runtime));
    assert(g_runtime.command.submit(g_runtime.command.context,"AT+CCLK?",5000,&g_runtime.operation));
    g_runtime.waiting=1;
    ABoxEc800At_Task(&at);
    trial_state=ABOX_MQTT_TRIAL_RESTORE_FAILED;
    TASK(60005);
    assert(g_runtime.adapter.result!=ABOX_ASYNC_PENDING && !g_runtime.waiting);
    assert(!ABoxEc800At_HasPending(&at,g_runtime.options.owner));
    /* Policy is checked on RDY even before the next product task runs. */
    ABoxEc800At_Feed(&at, (const uint8_t *)"RDY\r\nRDY\r\n", 10);
    assert(g_runtime.safety_locked && g_runtime.blocked && !NEEDS());
    assert(paused && !security && resets == 4);
    assert(!ABoxMqttRuntime_RecoveryComplete(&g_runtime));
    trial_state=ABOX_MQTT_TRIAL_FAILED;
    TASK(60005);
    assert(g_runtime.safety_locked && paused && !security);
    assert(!ABoxMqttRuntime_Apply(&g_runtime, &c, 60006));
    assert(ABoxMqttRuntime_RecoveryComplete(&g_runtime)); /* explicit external repair */
    assert(g_runtime.command.submit(g_runtime.command.context,"AT+CCLK?",5000,&g_runtime.operation));
    g_runtime.waiting=1; /* queued wait must also terminate on a safety latch */
    readback_failed=1;
    TASK(60007);
    assert(g_runtime.adapter.result!=ABOX_ASYNC_PENDING && !g_runtime.waiting);
    ABoxEc800At_Feed(&at, (const uint8_t *)"RDY\r\n", 5);
    assert(g_runtime.safety_locked && !NEEDS() && paused && !security);
    assert(!ABoxMqttRuntime_RecoveryComplete(&g_runtime));
    return 0;
}
