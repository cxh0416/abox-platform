#include "abox_app_service.h"
#include "abox_mqtt_ec800_rx.h"
#include <assert.h>
#include <string.h>

static uint32_t now;
static int power_done=1, maintenance_locked;
static unsigned restarts, reset_events, completions[2];
static ABoxEc800Rx rx;
static ABoxMqttEc800Rx mqtt_rx;
void EC_Power_Task(void) { }
int EC_Power_IsOnDone(void) { return power_done; }
void EC_Power_StartRestartSequence(void) { ++restarts; power_done=0; }
int ABoxBootV2App_IsBusy(void) { return 0; }
void ABoxBootV2App_Task(void) { }
static uint32_t tick(void *u) { (void)u; return now; }
static int write_at(void *u, const uint8_t *d, uint16_t n, uint32_t t)
{ (void)u; (void)d; (void)n; (void)t; return 1; }
static void done(ABoxEc800Result r, void *u)
{ assert(r==ABOX_EC800_RESULT_ERROR); ++*(unsigned *)u; }
static int rx_restart(void *u) { (void)u; ABoxEc800Rx_AfterRestart(&rx); return 1; }
static void cloud(void *u, uint32_t t) { (void)u; (void)t; }
static int error(void *u) { (void)u; return 0; }
static int busy(void *u) { (void)u; return maintenance_locked; }
static int not_ready(void *u) { (void)u; return 0; }
static void event(void *u, ABoxAppServiceEvent e) { (void)u; (void)e; }
static void modem_event(ABoxEc800Event e, const uint8_t *d, uint16_t n, void *u)
{
    (void)u;
    if (e==ABOX_EC800_EVENT_LINE && n==3 && !memcmp(d,"RDY",3)) {
        ++reset_events;
        ABoxMqttEc800Rx_Reset(&mqtt_rx);
    }
}
static void message(void *u, const uint8_t *t, size_t n, const uint8_t *p, size_t m)
{ (void)u; (void)t; (void)n; (void)p; (void)m; assert(0); }
static void feed_mqtt(void *u, const uint8_t *d, uint16_t n, uint32_t t)
{ ABoxMqttEc800Rx_Feed(u,d,n,t); }
static void poll_mqtt(void *u, uint32_t t) { ABoxMqttEc800Rx_Poll(u,t); }
static int collecting(void *u) { return ABoxMqttEc800Rx_IsCollecting(u); }
static int locked(void *u) { return ABoxMqttEc800Rx_IsLocked(u); }
int main(void)
{
    for (unsigned mode=0; mode<2; ++mode) {
        ABoxEc800At at;
        ABoxEc800AtPort p={.tick_ms=tick,.write=write_at};
        ABoxAppService service;
        ABoxEc800Recovery recovery;
        uint8_t dma[ABOX_EC800_RX_DMA_MIN], ring[ABOX_EC800_RX_RING_MIN];
        uint8_t header[96], topic[32], payload[32];
        ABoxEc800MqttRxPort demux={&mqtt_rx,feed_mqtt,poll_mqtt,collecting,locked};
        ABoxAppServicePort port={.restart_rx=rx_restart,.cloud=cloud,.event=event,
            .runtime_ready=not_ready,.transport_error=error,.maintenance_busy=busy,.recovery=&recovery};
        now=0; power_done=1; maintenance_locked=1; restarts=reset_events=0;
        memset(completions,0,sizeof(completions));
        assert(ABoxEc800At_Init(&at,&p));
        assert(ABoxEc800Rx_Init(&rx,dma,sizeof(dma),ring,sizeof(ring)));
        assert(ABoxMqttEc800Rx_Init(&mqtt_rx,header,sizeof(header),topic,sizeof(topic),payload,sizeof(payload),100,0,message,0));
        assert(ABoxEc800At_SetMqttReceiver(&at,&demux));
        assert(ABoxEc800At_Register(&at,ABOX_EC800_OWNER_MQTT,modem_event,0));
        ABoxEc800Recovery_Init(&recovery,120000);
        assert(ABoxAppService_Init(&service,&at,&rx,&port));
        ABoxAppService_Poll(&service,now); /* initial receive must start first */
        assert(ABoxEc800At_Submit(&at,"AT",20,ABOX_EC800_PRIORITY_NORMAL,5000,done,&completions[0]));
        ABoxEc800At_Task(&at);
        assert(ABoxEc800At_Submit(&at,"AT",21,ABOX_EC800_PRIORITY_NORMAL,5000,done,&completions[1]));
        if (!mode) {
            rx.head=rx.ring_size;
            assert(!ABoxEc800Rx_OnDmaEvent(&rx,1)); /* actual ring overflow */
        } else {
            const char *partial="+QMTRECV: 0,0,\"topic\",3,x";
            ABoxEc800At_Feed(&at,(const uint8_t *)partial,(uint16_t)strlen(partial));
            now=101;
        }
        ABoxAppService_Poll(&service,now);
        assert(at.quarantined && completions[0]==1 && completions[1]==1);
        assert(!restarts); /* restore/readback lock denies rail recovery */
        maintenance_locked=0;
        ABoxAppService_Poll(&service,now);
        assert(restarts==1 && service.reset_pending && !reset_events);
        ABoxAppService_Poll(&service,now+1);
        assert(!reset_events && at.quarantined);
        power_done=1;
        ABoxAppService_Poll(&service,now+2);
        assert(reset_events==1 && !service.reset_pending && !at.quarantined);
        assert(!ABoxMqttEc800Rx_IsLocked(&mqtt_rx) && !at.mqtt_rx_active);
        ABoxAppService_Poll(&service,now+3);
        assert(reset_events==1 && restarts==1);
        assert(completions[0]==1 && completions[1]==1);
    }
    return 0;
}
