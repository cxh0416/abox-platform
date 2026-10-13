#include "abox_app_service.h"
#include <assert.h>
#include <string.h>

static uint32_t now;
static unsigned attempts, failures, clouds, otas, ats, completed[2], resets, unavailable;
static ABoxEc800Rx rx;
static int restart_interrupt;
void EC_Power_Task(void) { }
int EC_Power_IsOnDone(void) { return 1; }
void EC_Power_StartRestartSequence(void) { assert(0); }
int ABoxBootV2App_IsBusy(void) { return 0; }
void ABoxBootV2App_Task(void) { ++otas; }
static uint32_t tick(void *u) { (void)u; return now; }
static int write_at(void *u, const uint8_t *d, uint16_t n, uint32_t t)
{ (void)u; (void)d; (void)n; (void)t; ++ats; return 1; }
static int restart_rx(void *u)
{
    (void)u; ++attempts;
    if (failures) { --failures; return 0; }
    ABoxEc800Rx_AfterRestart(&rx);
    if (restart_interrupt) { restart_interrupt=0; ABoxEc800Rx_RequestRestart(&rx); }
    return 1;
}
static void cloud(void *u, uint32_t t) { (void)u; (void)t; ++clouds; }
static void ready(void *u, int value, uint32_t t)
{ (void)u; (void)t; if (!value) ++unavailable; }
static void done(ABoxEc800Result r, void *u)
{ assert(r==ABOX_EC800_RESULT_ERROR); ++*(unsigned *)u; }
static void event(ABoxEc800Event e, const uint8_t *d, uint16_t n, void *u)
{ (void)u; if (e==ABOX_EC800_EVENT_LINE && n==3 && !memcmp(d,"RDY",3)) ++resets; }
int main(void)
{
    for (unsigned wrap=0; wrap<3; ++wrap) {
        ABoxEc800At at;
        ABoxEc800AtPort wire={.tick_ms=tick,.write=write_at};
        ABoxAppService service;
        ABoxAppServicePort port={.restart_rx=restart_rx,.cloud=cloud,.ready=ready};
        uint8_t dma[ABOX_EC800_RX_DMA_MIN], ring[ABOX_EC800_RX_RING_MIN];
        now=wrap==0 ? 0 : wrap==1 ? 0x000FFFC0U : 0xFFFFFFC0U;
        attempts=clouds=otas=ats=resets=unavailable=0;
        failures=2; completed[0]=completed[1]=0;
        assert(ABoxEc800At_Init(&at,&wire));
        assert(ABoxEc800At_Register(&at,20,event,NULL));
        assert(ABoxEc800Rx_Init(&rx,dma,sizeof(dma),ring,sizeof(ring)));
        assert(ABoxAppService_Init(&service,&at,&rx,&port));
        assert(ABoxEc800At_Submit(&at,"AT",20,ABOX_EC800_PRIORITY_NORMAL,1000,done,&completed[0]));
        assert(ABoxEc800At_Submit(&at,"AT",21,ABOX_EC800_PRIORITY_NORMAL,1000,done,&completed[1]));
        ABoxAppService_Poll(&service,now);
        assert(attempts==1 && service.rx_failed && at.quarantined && rx.restart_requested);
        assert(!clouds && !otas && !ats && completed[0]==1 && completed[1]==1);
        for (unsigned i=0;i<127;++i) { ++now; ABoxAppService_Poll(&service,now); }
        assert(attempts==1 && !clouds);
        ++now; ABoxAppService_Poll(&service,now);
        assert(attempts==2 && !clouds);
        now+=128; ABoxAppService_Poll(&service,now);
        assert(attempts==3 && !service.rx_failed && !rx.restart_requested);
        assert(clouds==1 && otas==1 && !ats && unavailable>=2);
        /* UART success cannot assert physical reset or release safety policy. */
        assert(at.quarantined && !resets);
        memcpy(dma,"RDY\r\n",5);
        assert(ABoxEc800Rx_OnDmaEvent(&rx,5));
        ABoxAppService_Poll(&service,now);
        assert(!at.quarantined && resets==1);
        assert(ABoxEc800At_Submit(&at,"AT",20,ABOX_EC800_PRIORITY_NORMAL,1000,done,&completed[0]));
        ABoxEc800At_Task(&at);
        assert(ABoxEc800At_Submit(&at,"AT",21,ABOX_EC800_PRIORITY_NORMAL,1000,done,&completed[1]));
        failures=1; ABoxEc800Rx_RequestRestart(&rx);
        ABoxAppService_Poll(&service,now);
        assert(service.rx_failed && completed[0]==2 && completed[1]==2 && at.quarantined);
        now+=128; ABoxAppService_Poll(&service,now);
        assert(!service.rx_failed && completed[0]==2 && completed[1]==2);
        assert(ats==1 && resets==1); /* No retransmission of a cancelled operation. */
        restart_interrupt=1; ABoxEc800Rx_RequestRestart(&rx);
        unsigned old_clouds=clouds;
        ABoxAppService_Poll(&service,now);
        assert(service.rx_failed && rx.restart_requested && clouds==old_clouds);
        now+=128; ABoxAppService_Poll(&service,now);
        assert(!service.rx_failed && clouds==old_clouds+1 && ats==1);
    }
    return 0;
}
