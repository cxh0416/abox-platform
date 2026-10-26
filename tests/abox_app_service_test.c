#include "abox_app_service.h"
#include <assert.h>
#include <string.h>
static char trace[32];
static unsigned used;
static int enabled = 1, restart;
static int ota_busy, maintenance_busy, blocked, runtime_ready = 1;
static int power_done = 1;
static ABoxAppService service;
static void record(char c) { trace[used++] = c; trace[used] = 0; }
void EC_Power_Task(void) { record('P'); }
int EC_Power_IsOnDone(void) { return power_done; }
void EC_Power_StartRestartSequence(void) { record('Z'); power_done=0; }
void ABoxEc800At_OnModemReset(ABoxEc800At *at) { (void)at; record('F'); }
int ABoxBootV2App_IsBusy(void) { return ota_busy; }
void ABoxEc800At_Feed(ABoxEc800At *at, const uint8_t *data, uint16_t length)
{ (void)at; (void)data; (void)length; }
void ABoxEc800At_Task(ABoxEc800At *at) { (void)at; record('A'); }
void ABoxEc800At_SetRxOverflowCount(ABoxEc800At *at, uint32_t count) { (void)at; (void)count; }
void ABoxEc800At_AbortAll(ABoxEc800At *at, ABoxEc800Result result) { (void)at; (void)result; record('X'); }
uint32_t ABoxEc800Rx_Drain(ABoxEc800Rx *rx, ABoxEc800RxSink sink, void *context, uint32_t limit)
{ (void)rx; (void)sink; (void)context; (void)limit; record('R'); return 0; }
uint32_t ABoxEc800Rx_OverflowCount(const ABoxEc800Rx *rx) { (void)rx; return 0; }
int ABoxEc800Rx_TakeRestartRequest(ABoxEc800Rx *rx) { (void)rx; return restart; }
void ABoxEc800Rx_RequestRestart(ABoxEc800Rx *rx) { (void)rx; restart=1; }
void ABoxBootV2App_Task(void) { record('O'); }
static int restart_rx(void *context) { (void)context; record('S'); restart = 0; return 1; }
static int gate(void *context) { (void)context; record('G'); return enabled; }
static void after(void *context, uint32_t now) { (void)context; (void)now; record('D'); }
static void before(void *context, uint32_t now) { (void)context; (void)now; record('B'); }
static void enrollment(void *context, uint32_t now) { (void)context; (void)now; record('E'); }
static void cloud(void *context, uint32_t now)
{ (void)context; record('C'); ABoxAppService_Poll(&service, now); }
static int hold(void *context, uint32_t now) { (void)context; (void)now; record('H'); return 0; }
static void runtime(void *context, uint32_t now) { (void)context; (void)now; record('U'); }
static void transport(void *context, uint32_t now) { (void)context; (void)now; record('T'); }
static int is_runtime_ready(void *context) { (void)context; return runtime_ready; }
static int is_blocked(void *context) { (void)context; return blocked; }
static int is_maintenance_busy(void *context) { (void)context; return maintenance_busy; }
static void event(void *context, ABoxAppServiceEvent value)
{ (void)context; assert(value == ABOX_APP_NETWORK_CHANGED); record('N'); }
int main(void)
{
    ABoxEc800At at = {0};
    ABoxEc800Rx rx = {0};
    ABoxAppServicePort port = {.restart_rx=restart_rx, .enabled=gate, .after_at=after,
        .before_enrollment=before, .enrollment=enrollment, .cloud=cloud};
    assert(ABoxAppService_Init(&service, &at, &rx, &port));
    ABoxAppService_Poll(&service, 1U);
    assert(strcmp(trace, "PSRGADOBEC") == 0); /* initial RX before communication; suppress recursion */
    used = 0; enabled = 0; restart = 1;
    ABoxAppService_Poll(&service, 2U);
    assert(strcmp(trace, "PXSRG") == 0); /* discard damaged RX before parsing, even when gated */
    assert(!service.polling);
    ABoxEc800Recovery recovery;
    ABoxEc800Recovery_Init(&recovery, 120000U);
    port.runtime = runtime; port.runtime_ready = is_runtime_ready;
    port.transport = transport; port.transport_error = is_blocked;
    port.maintenance_busy = is_maintenance_busy; port.recovery = &recovery;
    assert(!ABoxAppService_Init(&service, &at, &rx, &port));
    port.event = event;
    assert(ABoxAppService_Init(&service, &at, &rx, &port));
    used=0; enabled=1; blocked=1; ota_busy=1;
    ABoxAppService_Poll(&service, 3U);
    assert(strcmp(trace, "PSRGADOBEUTC") == 0); /* RX starts; OTA owns resources: no reset */
    used=0; ota_busy=0; maintenance_busy=1;
    ABoxAppService_Poll(&service, 4U);
    assert(strcmp(trace, "PRGADOBEUTC") == 0); /* Includes failed restore lock */
    used=0; maintenance_busy=0;
    ABoxAppService_Poll(&service, 5U);
    assert(strcmp(trace, "PRGADOBEUTNXZ") == 0); /* invalidate and terminate before rail restart */
    assert(service.reset_pending);
    used=0;
    ABoxAppService_Poll(&service, 6U);
    assert(strcmp(trace, "PRGADOBEUTC") == 0); /* recovery cooldown */
    used=0; power_done=1;
    ABoxAppService_Poll(&service, 6U);
    assert(strcmp(trace, "PSFRGADOBEUTC") == 0); /* RX rearm precedes the reset notification */
    assert(!service.reset_pending);
    used=0; blocked=0; runtime_ready=0;
    ABoxAppService_Poll(&service, 7U);
    assert(strcmp(trace, "PRGADOBEU") == 0); /* runtime gates transport */
    used=0; runtime_ready=1; port.transport_first=1;
    ABoxAppService_Poll(&service, 8U);
    assert(strcmp(trace, "PRGADOBETUC") == 0); /* baremetal compatibility order */
    used=0; port.before_cloud=hold;
    ABoxAppService_Poll(&service, 9U);
    assert(strcmp(trace, "PRGADOBEH") == 0); /* async stop holds all connection work */
    used=0; port.before_cloud=0; port.transport_first=0;
    port.recovery_after_cloud=1; blocked=1;
    ABoxAppService_Poll(&service, 120005U);
    assert(strcmp(trace, "PRGADOBEUTCNXZ") == 0); /* process deferred requests before restart admission */
    used=0; runtime_ready=0; service.reset_pending=0; power_done=1;
    ABoxAppService_Poll(&service, 240005U);
    assert(strcmp(trace, "PRGADOBEUNXZ") == 0); /* failed runtime cannot skip recovery */
    used=0; port.before_cloud=hold; service.reset_pending=0; power_done=1;
    ABoxAppService_Poll(&service, 360005U);
    assert(strcmp(trace, "PRGADOBEHNXZ") == 0);
    used=0; maintenance_busy=1;
    ABoxAppService_Poll(&service, 480005U);
    assert(strcmp(trace, "PRGADOBEH") == 0); /* restore failure still locks */
    return 0;
}
