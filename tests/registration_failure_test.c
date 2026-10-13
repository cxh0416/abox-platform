#include "abox_mqtt_ec800_bridge.h"
#include "abox_ec800_diagnostics.h"
#include "abox_enrollment_service.h"
#include <assert.h>
#include <string.h>

static unsigned writes, events;
static uint32_t tick(void *u) { (void)u; return 0; }
static int write_at(void *u, const uint8_t *d, uint16_t n, uint32_t t)
{ (void)u; (void)d; (void)n; (void)t; ++writes; return 1; }
static void observer(ABoxEc800Event e, const uint8_t *d, uint16_t n, void *u)
{ (void)e; (void)d; (void)n; (void)u; ++events; }
static void message(void *u, const char *t, const char *p)
{ (void)u; (void)t; (void)p; assert(0); }
static int yes(void *u) { (void)u; return 1; }
static int no(void *u) { (void)u; return 0; }
static int identity(void *u, ABoxEnrollmentIdentity *i) { (void)u; (void)i; return 0; }
static void pause_mqtt(void *u, uint8_t p) { (void)u; (void)p; }
static int trial(void *u, const ABoxEnrollmentCredential *c) { (void)u; (void)c; return 0; }
static void cancel(void *u) { (void)u; }
static void http_done(void *u, uint16_t s, size_t n, int clean)
{ (void)u; (void)s; (void)n; (void)clean; assert(0); }
static unsigned slots(const ABoxEc800At *at)
{
    unsigned n=0;
    for (unsigned i=0; i<ABOX_EC800_AT_HANDLER_SIZE; ++i) n += at->handlers[i].callback != NULL;
    return n;
}
int main(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port={.tick_ms=tick, .write=write_at};
    ABoxEc800Iccid iccid;
    ABoxEc800Diagnostics diag;
    ABoxEnrollmentEc800Http http;
    ABoxEnrollmentService service;
    ABoxMqttEc800Bridge bridge;
    uint8_t header[192], topic[128], payload[128], outgoing[128];
    ABoxMqttEc800Buffers mqtt_buffers={header,topic,payload,sizeof(header),sizeof(topic),
        sizeof(payload),outgoing,sizeof(outgoing)};
    ABoxMqttEc800Config config={.host="broker",.port=1883,.client_id="device",
        .command_timeout_ms=100,.open_timeout_ms=100,.connect_timeout_ms=100,
        .subscribe_timeout_ms=100,.publish_timeout_ms=100,.retry_delay_ms=100};
    ABoxMqttEc800BridgeHooks hooks={.message=message};
    char url[192], body[512], response[769], id[48], token[64];
    ABoxEnrollmentBuffers buffers={url,body,response,id,token,sizeof(url),sizeof(body),
        sizeof(response),sizeof(id),sizeof(token)};
    ABoxEnrollmentServicePort policy={NULL,tick,no,no,identity,pause_mqtt,trial,no,cancel};
    assert(ABoxEc800At_Init(&at,&port));
    for (unsigned i=0; i<ABOX_EC800_AT_HANDLER_SIZE; ++i)
        assert(ABoxEc800At_Register(&at,(ABoxEc800Owner)(20+i),observer,NULL));
    assert(!ABoxEc800Iccid_Init(&iccid,&at,30));
    assert(!ABoxEc800Iccid_Request(&iccid,0) && !iccid.at);
    assert(!ABoxEc800Diagnostics_Init(&diag,&at,30));
    ABoxEc800Diagnostics_Poll(&diag,0,1,0,0,0);
    assert(!diag.at && !ABoxEc800At_IsBusy(&at));
    assert(!ABoxEnrollmentEc800Http_Init(&http,&at,30,"UFS:ca.pem",http_done,NULL));
    assert(!http.at && !ABoxEnrollmentEc800Http_Ready(&http));
    assert(!ABoxEnrollmentEc800Http_Post(&http,"https://server","{}",response,sizeof(response)));
    assert(!ABoxEnrollmentService_Init(&service,&at,30,"UFS:ca.pem","https://server",&buffers,1,&policy));
    ABoxEnrollmentService_Poll(&service);
    assert(!service.initialized && !service.http.at);
    assert(slots(&at)==ABOX_EC800_AT_HANDLER_SIZE && !writes);

    /* Only the MQTT slot fits: ICCID fails, so no partial demux survives. */
    assert(ABoxEc800At_Register(&at,25,NULL,NULL));
    assert(!ABoxMqttEc800Bridge_Init(&bridge,&at,&config,&mqtt_buffers,&hooks,30,0));
    assert(!bridge.initialized && !bridge.transport.initialized && !at.mqtt_rx.context);
    assert(slots(&at)==ABOX_EC800_AT_HANDLER_SIZE-1);
    ABoxMqttEc800Bridge_Poll(&bridge,1000);
    ABoxEc800At_Feed(&at,(const uint8_t *)"RDY\r\n",5);
    assert(events==ABOX_EC800_AT_HANDLER_SIZE-1 && !writes);
    assert(ABoxEc800At_Register(&at,24,NULL,NULL));
    assert(ABoxMqttEc800Bridge_Init(&bridge,&at,&config,&mqtt_buffers,&hooks,30,0));
    assert(bridge.initialized && slots(&at)==ABOX_EC800_AT_HANDLER_SIZE);

    assert(ABoxEc800At_Init(&at,&port));
    buffers.body=NULL;
    assert(!ABoxEnrollmentService_Init(&service,&at,30,"UFS:ca.pem","https://server",&buffers,1,&policy));
    assert(!slots(&at));
    buffers.body=body;
    assert(ABoxEnrollmentService_Init(&service,&at,30,"UFS:ca.pem","https://server",&buffers,1,&policy));
    assert(!ABoxEnrollmentService_RequirePsk(&service,NULL,0));
    assert(!service.initialized && !service.http.at && !slots(&at));
    policy.network_ready=yes;
    assert(ABoxEnrollmentService_Init(&service,&at,30,"UFS:ca.pem","https://server",&buffers,1,&policy));
    assert(ABoxEnrollmentService_RequirePsk(&service,(const uint8_t *)"CA",2));
    assert(service.initialized && !ABoxEnrollmentEc800Http_Ready(&service.http));
    assert(slots(&at)==1 && !writes); /* CA still requires normal verification. */
    return 0;
}
