#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "abox_mqtt_ec800.h"
static uint32_t now;
static unsigned writes;
static uint32_t tick(void *context) { (void)context; return now; }
static int send(void *context,const uint8_t *bytes,uint16_t length,uint32_t timeout)
{ (void)context;(void)bytes;(void)length;(void)timeout; ++writes; return 1; }
static void message(void *context,const uint8_t *topic,size_t topic_length,const uint8_t *payload,size_t payload_length)
{ (void)context;(void)topic;(void)topic_length;(void)payload;(void)payload_length; }
int main(void)
{
    ABoxMqttBackoff b,other;
    const uint32_t limits[]={4000,8000,16000,32000,60000,60000};
    ABoxMqttBackoff_Init(&b,123); ABoxMqttBackoff_Init(&other,456);
    uint32_t a=ABoxMqttBackoff_Initial(&b),c=ABoxMqttBackoff_Initial(&other);
    assert(a<=4000 && c<=4000 && a!=c);
    for (unsigned i=0;i<6;++i) { uint32_t delay=ABoxMqttBackoff_Failed(&b); assert(delay>=limits[i]/2 && delay<=limits[i]); }
    ABoxMqttBackoff_Online(&b,UINT32_MAX-100000U);
    ABoxMqttBackoff_Poll(&b,199998U); assert(b.failures==4);
    ABoxMqttBackoff_Poll(&b,199999U); assert(b.failures==0);
    assert(ABoxMqttBackoff_Failed(&b)<=4000);
    ABoxEc800At at; ABoxMqttEc800 mqtt;
    ABoxEc800AtPort at_port={NULL,tick,send,NULL,NULL};
    const char *topics[]={"/zxwl/abox/device/request"};
    ABoxMqttEc800Config config={0};
    config.apn="CMNET";config.host="broker";config.client_id="device";config.username="u";config.password="p";
    config.port=1883;config.subscriptions=topics;config.subscription_count=1;
    config.command_timeout_ms=config.open_timeout_ms=config.connect_timeout_ms=1000;
    config.subscribe_timeout_ms=config.publish_timeout_ms=config.retry_delay_ms=1000;
    uint8_t header[128],topic[128],payload[1025],publish[1024];
    ABoxMqttEc800Buffers buffers={header,topic,payload,sizeof(header),sizeof(topic),sizeof(payload),publish,sizeof(publish)};
    ABoxMqttEc800Callbacks callbacks={0};
    callbacks.message=message;
    assert(ABoxEc800At_Init(&at,&at_port));
    assert(ABoxMqttEc800_Init(&mqtt,&at,&config,&buffers,&callbacks,0));
    assert(ABoxMqttEc800_UseV4Backoff(&mqtt,123));
    ABoxMqttEc800_SetSecurityReady(&mqtt,1);
    assert(ABoxMqttEc800_Start(&mqtt,now)); assert(mqtt.state==ABOX_MQTT_EC800_RETRY_WAIT);
    uint32_t delay=mqtt.retry_wait_ms;
    assert(delay<=4000 && ABoxMqttEc800_Start(&mqtt,now) && delay==mqtt.retry_wait_ms);
    for (now=0;now<delay;++now) { ABoxMqttEc800_Poll(&mqtt,now);ABoxEc800At_Task(&at);assert(!writes); }
    now=delay+1000;ABoxMqttEc800_Poll(&mqtt,now);ABoxMqttEc800_Poll(&mqtt,now);ABoxEc800At_Task(&at);
    assert(writes==1 && mqtt.state==ABOX_MQTT_EC800_CONFIGURING);
    const char error[]="\r\nERROR\r\n";
    ABoxEc800At_Feed(&at,(const uint8_t *)error,(uint16_t)strlen(error));
    ABoxMqttEc800_Poll(&mqtt,now);
    assert(mqtt.state==ABOX_MQTT_EC800_RETRY_WAIT && mqtt.retry_wait_ms>=2000 &&
        mqtt.retry_wait_ms<=4000 && mqtt.backoff.failures==1);
    puts("PASS jitter, capped backoff, stable-online reset/tick wrap and actual transport opt-in");
    return 0;
}
