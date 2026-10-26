#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "abox_mqtt_request.h"
static int action(void *context, const cJSON *params)
{
    const cJSON *a=cJSON_GetObjectItemCaseSensitive(params,"action");
    return cJSON_IsString(a) && !strcmp(a->valuestring,context);
}
static uint16_t params_valid(void *context, const cJSON *params)
{ (void)context; return cJSON_IsObject(params) ? 200U : 4001U; }
static int normalized(void *context,ABoxCanonicalWriter write,void *writer)
{ (void)context;const char *text="{\"action\":\"ascend\"}";return write(writer,text,strlen(text)); }
static int number_field(void *context,const cJSON *root,const cJSON *number)
{ (void)context;(void)root;return number->string && !strcmp(number->string,"angle"); }
static void decode(ABoxMqttRequest *r, const char *text, uint16_t expected)
{ assert(ABoxMqttRequest_Decode(r,text,strlen(text),NULL,NULL)==expected); }
int main(void)
{
    ABoxMqttRequest r={0}; const ABoxMqttOperation *op;
    ABoxClockSnapshot clock={0}; uint8_t digest[32];
    ABoxMqttOperation ops[]={
        {"wing_control","wing",ABOX_MQTT_COMMAND_DISCRETE_ACTION,ABOX_MQTT_EXPIRY_REQUIRED,5000,action,params_valid,"ascend"},
        {"wing_control","wing",ABOX_MQTT_COMMAND_SAFETY_STOP,ABOX_MQTT_EXPIRY_FORBIDDEN,0,action,params_valid,"stop"}
    };
    const char *request="{\"version\":\"4.0\",\"timestamp\":1000,\"data\":{\"requestId\":\"x:1\",\"command\":\"wing_control\",\"expiresAt\":6000,\"params\":{\"action\":\"ascend\"}}}";
    assert(ABoxMqttV4_IdValid("_device-1",ABOX_MQTT_ID_DEVICE));
    assert(!ABoxMqttV4_IdValid("a/b",ABOX_MQTT_ID_DEVICE));
    assert(!ABoxMqttV4_IdValid("a.b",ABOX_MQTT_ID_DEVICE));
    assert(!ABoxMqttV4_IdValid("-x",ABOX_MQTT_ID_REQUEST));
    assert(ABoxMqttV4_IdValid("x-1.2:3",ABOX_MQTT_ID_REQUEST));
    assert(!ABoxMqttV4_NameValid("x__y")); assert(!ABoxMqttV4_NameValid("x_"));
    assert(!ABoxMqttV4_ProfileVersionValid("01.0")); assert(!ABoxMqttV4_ProfileVersionValid("1.65536"));
    assert(ABoxMqttV4_ProfileVersionValid("65535.0"));
    decode(&r,request,200); memcpy(digest,r.digest,32);
    assert(ABoxMqttRequest_Normalize(&r,normalized,NULL) && !memcmp(digest,r.digest,32));
    assert(ABoxMqttRequest_Resolve(&r,ops,2,&op)==200 && op==ops);
    assert(ABoxMqttRequest_CheckNew(&r,op,&clock)==4003);
    clock.quality=ABOX_CLOCK_SYNCHRONIZED; clock.utc_ms=5990; clock.error_ms=10;
    assert(ABoxMqttRequest_CheckNew(&r,op,&clock)==4003);
    clock.utc_ms=6000; assert(ABoxMqttRequest_CheckNew(&r,op,&clock)==4005);
    clock.utc_ms=5000; assert(ABoxMqttRequest_CheckNew(&r,op,&clock)==200);
    ops[1]=ops[0]; assert(ABoxMqttRequest_Resolve(&r,ops,2,&op)==4001); /* Ambiguous route. */
    ABoxMqttRequest_Close(&r);
    decode(&r,"{\"timestamp\":1000,\"version\":\"4.0\",\"data\":{\"params\":{\"action\":\"ascend\"},\"expiresAt\":6000,\"command\":\"wing_control\",\"requestId\":\"x:1\"}}",200);
    assert(!memcmp(digest,r.digest,32)); ABoxMqttRequest_Close(&r);
    decode(&r,"{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"s\",\"command\":\"sync_state\",\"params\":null}}",200);
    assert(ABoxMqttRequest_Resolve(&r,ops,2,&op)==200 && op->classification==ABOX_MQTT_COMMAND_QUERY);
    ABoxMqttRequest_Close(&r);
    decode(&r,"{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"s\",\"command\":\"sync_state\",\"params\":{},\"expiresAt\":5}}",200);
    assert(ABoxMqttRequest_Resolve(&r,ops,2,&op)==4001); ABoxMqttRequest_Close(&r);
    decode(&r,"{\"version\":\"4.0\",\"timestamp\":1e3,\"data\":{\"requestId\":\"x\"}}",4001);
    assert(!r.request_id); ABoxMqttRequest_Close(&r);
    ops[1]=(ABoxMqttOperation){"wing_control","wing",ABOX_MQTT_COMMAND_SAFETY_STOP,
        ABOX_MQTT_EXPIRY_FORBIDDEN,0,action,params_valid,"stop"};
    decode(&r,"{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"stop1\",\"command\":\"wing_control\",\"params\":{\"action\":\"stop\"}}}",200);
    clock.quality=ABOX_CLOCK_UNTRUSTED;
    assert(ABoxMqttRequest_Resolve(&r,ops,2,&op)==200 && op->classification==ABOX_MQTT_COMMAND_SAFETY_STOP);
    assert(ABoxMqttRequest_CheckNew(&r,op,&clock)==200);ABoxMqttRequest_Close(&r);
    const char *fraction="{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"n\",\"command\":\"motion\",\"params\":{\"angle\":1.5}}}";
    decode(&r,fraction,4001);ABoxMqttRequest_Close(&r);
    assert(ABoxMqttRequest_Decode(&r,fraction,strlen(fraction),number_field,NULL)==200);ABoxMqttRequest_Close(&r);
    char oversized[1000],value[800];memset(value,'x',799);value[799]=0;
    snprintf(oversized,sizeof(oversized),"{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"big\",\"command\":\"query\",\"params\":{\"x\":\"%s\"}}}",value);
    decode(&r,oversized,4001);assert(r.request_id && !strcmp(r.request_id,"big"));ABoxMqttRequest_Close(&r);
    decode(&r,"{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"x\",\"requestId\":\"y\"}}",4001);
    assert(!r.request_id); ABoxMqttRequest_Close(&r);
    decode(&r,"{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"a/b\",\"command\":\"query\",\"params\":null}}",4001);
    assert(!r.request_id); ABoxMqttRequest_Close(&r);
    char large[1100]; memset(large,'x',sizeof(large));
    assert(ABoxMqttRequest_Decode(&r,large,sizeof(large),NULL,NULL)==4001);
    puts("PASS public request validation, canonical identity, expiry and parameter-level routing");
    return 0;
}
