#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "abox_time_client.h"
#include "abox_mqtt_v4.h"
#include "cJSON.h"
#include "abox_clock.h"
static char request_id[65]; static unsigned sends,applies;
static int send(void *context,const char *topic,const char *packet,size_t length)
{
    (void)context; assert(strstr(topic,"/device/internal/time/request"));
    cJSON *root=cJSON_ParseWithLength(packet,length);
    const cJSON *data=cJSON_GetObjectItemCaseSensitive(root,"data");
    strcpy(request_id,cJSON_GetObjectItemCaseSensitive(data,"requestId")->valuestring);
    cJSON_Delete(root); ++sends; return 1;
}
static int apply(void *context,uint64_t utc,uint32_t error,uint64_t sent,uint64_t received)
{ (void)context; assert(utc==1770186088333ULL && error==10 && received-sent==20); ++applies; return 1; }
int main(void)
{
    ABoxTimeClient c; ABoxTimeClientPort port={NULL,send,apply}; char response[256];
    assert(ABoxTimeClient_Init(&c,"device",123,&port));
    ABoxTimeClient_Poll(&c,0,1,0,0);assert(!sends);
    ABoxTimeClient_Poll(&c,1,1,0,0);assert(sends==1);
    snprintf(response,sizeof(response),"{\"version\":\"2.0\",\"timestamp\":0,\"data\":{\"requestId\":\"%s\",\"code\":200,\"result\":{\"serverUtcMs\":1770186088333,\"errorMs\":10}}}",request_id);
    assert(ABoxTimeClient_Message(&c,c.response_topic,response,strlen(response),0,1,20)); assert(c.sampling.pending);
    assert(ABoxTimeClient_Message(&c,c.response_topic,response,strlen(response),1,2,20)); assert(c.sampling.pending);
    assert(ABoxTimeClient_Message(&c,c.response_topic,response,strlen(response),1,1,20)); assert(!c.sampling.pending);
    for (uint64_t now=1020;now<=2040;now+=1020) {
        ABoxTimeClient_Poll(&c,1,1,now,0);
        char utc[21]; assert(ABoxMqttV4_FormatInteger(utc,sizeof(utc),1770186088333ULL+now));
        snprintf(response,sizeof(response),"{\"version\":\"2.0\",\"timestamp\":0,\"data\":{\"requestId\":\"%s\",\"code\":200,\"result\":{\"serverUtcMs\":%s,\"errorMs\":10}}}",request_id,utc);
        ABoxTimeClient_Message(&c,c.response_topic,response,strlen(response),1,1,now+20);
    }
    ABoxTimeClient_Poll(&c,1,1,3060,0);assert(applies==1);
    ABoxTimeClient_Poll(&c,1,2,4000,0);assert(c.sampling.pending && sends==4);
    const char *bad="{\"version\":\"2.0\",\"version\":\"2.0\"}";
    ABoxTimeClient_Message(&c,c.response_topic,bad,strlen(bad),1,2,4020);assert(c.sampling.pending);
    uint32_t sequence=c.sampling.sequence;
    assert(ABoxTimeClient_SetDevice(&c,"device"));assert(sequence==c.sampling.sequence && !c.sampling.pending);
    assert(!ABoxTimeClient_SetDevice(&c,"a/b"));
    assert(!ABoxTimeClient_Init(&c,"device",123,NULL));
    unsigned previous=sends;ABoxTimeClient_Poll(&c,1,4,5000,0);assert(sends==previous);
    assert(!ABoxTimeClient_Init(&c,"a/b",123,&port));
    ABoxTimeClient_Poll(&c,1,4,6000,0);assert(sends==previous);
    puts("PASS shared time wire adapter: authenticated generation, strict JSON and ID continuity");
    return 0;
}
