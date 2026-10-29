#include "abox_time_client.h"
#include "abox_mqtt_request.h"
#include <stdio.h>
#include <string.h>
static int send(void *context, const char *id)
{
    ABoxTimeClient *c=context; char timestamp[21],packet[192],topic[72]; int n;
    size_t length = strlen(c->response_topic);
    if (length < sizeof("response") - 1U || length >= sizeof(topic)) return 0;
    memcpy(topic, c->response_topic, length - (sizeof("response") - 1U));
    memcpy(topic + length - (sizeof("response") - 1U), "request", sizeof("request"));
    if (!ABoxMqttV4_FormatInteger(timestamp,sizeof(timestamp),c->utc)) return 0;
    n=snprintf(packet,sizeof(packet),"{\"version\":\"2.0\",\"timestamp\":%s,\"data\":{\"requestId\":\"%s\",\"command\":\"sync_time\",\"params\":{}}}",timestamp,id);
    return n>0 && (size_t)n<sizeof(packet) && c->port.send(c->port.context,topic,packet,(size_t)n);
}
static int apply(void *context, uint64_t utc, uint32_t error, uint64_t sent, uint64_t received)
{
    ABoxTimeClient *c=context;
    return c->port.apply(c->port.context,utc,error,sent,received);
}
int ABoxTimeClient_SetDevice(ABoxTimeClient *c, const char *id)
{
    uint32_t sequence;
    if (!c || !ABoxMqttV4_IdValid(id,ABOX_MQTT_ID_DEVICE)) return 0;
    sequence=c->sampling.sequence; ABoxTimeSync_Init(&c->sampling); c->sampling.sequence=sequence;
    snprintf(c->response_topic,sizeof(c->response_topic),"/zxwl/abox/%s/internal/time/response",id);
    c->ready=0; c->generation=0; return 1;
}
int ABoxTimeClient_Init(ABoxTimeClient *c, const char *id, uint64_t seed, const ABoxTimeClientPort *p)
{
    if (!c) return 0;
    memset(c,0,sizeof(*c));
    if (!p || !p->send || !p->apply || !seed) return 0;
    c->port=*p; c->seed=seed;
    if (!ABoxTimeClient_SetDevice(c,id)) { memset(c,0,sizeof(*c)); return 0; }
    return 1;
}
void ABoxTimeClient_Poll(ABoxTimeClient *c, int authenticated, uint32_t generation, uint64_t mono, uint64_t utc)
{
    ABoxTimeSyncPort port={c,send,apply};
    if (!c || !c->port.send || !c->port.apply) return;
    if (!authenticated || !generation || c->generation!=generation) {
        c->sampling.ready=c->sampling.pending=c->sampling.attempts=c->sampling.have_best=0;
    }
    c->generation=generation; c->ready=authenticated && generation!=0;
    c->utc=utc<=9007199254740991ULL ? utc : 0;
    /* Refuse wrap instead of reusing a time requestId within this boot. */
    ABoxTimeSync_Poll(&c->sampling,&port,c->ready && c->sampling.sequence!=UINT32_MAX,mono,c->seed);
}
static int integer(const cJSON *value, uint64_t *out)
{
    if (!cJSON_IsNumber(value) || value->valuedouble<0 || value->valuedouble>9007199254740991.0 ||
        value->valuedouble!=(double)(uint64_t)value->valuedouble) return 0;
    *out=(uint64_t)value->valuedouble; return 1;
}
int ABoxTimeClient_Message(ABoxTimeClient *c, const char *topic, const char *packet, size_t size,
    int authenticated, uint32_t generation, uint64_t received)
{
    cJSON *root; const cJSON *data,*id,*version,*code,*result;
    uint64_t utc,error;
    if (!c || !topic || strcmp(c->response_topic,topic)) return 0;
    if (!authenticated || !c->ready || !generation || generation!=c->generation || !packet || size>1024) return 1;
    root=ABoxMqttDigest_Parse(packet,size);
    if (!root) return 1;
    data=cJSON_GetObjectItemCaseSensitive(root,"data");
    version=cJSON_GetObjectItemCaseSensitive(root,"version");
    id=cJSON_GetObjectItemCaseSensitive(data,"requestId");
    code=cJSON_GetObjectItemCaseSensitive(data,"code");
    result=cJSON_GetObjectItemCaseSensitive(data,"result");
    if (cJSON_IsObject(root) && cJSON_IsObject(data) && cJSON_IsString(version) && !strcmp(version->valuestring,"2.0") &&
        cJSON_IsString(id) && cJSON_IsNumber(code) && code->valuedouble==200 &&
        integer(cJSON_GetObjectItemCaseSensitive(result,"serverUtcMs"),&utc) &&
        integer(cJSON_GetObjectItemCaseSensitive(result,"errorMs"),&error) && error<=ABOX_CLOCK_MAX_REFERENCE_ERROR_MS)
        (void)ABoxTimeSync_Response(&c->sampling,id->valuestring,utc,(uint32_t)error,received);
    cJSON_Delete(root); return 1;
}
