#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "abox_mqtt_registry_dump.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define EQ(path,s) {.kind=ABOX_MATCH_EQUALS,.field=path,.equals={.type=ABOX_MATCH_STRING,.value.string=s}}
#define CONTROL_DESCRIPTION "Control the selected actuator channel."
#define MOVE_SCHEMA "{\"type\":\"object\",\"required\":[\"channel\",\"action\"],\"properties\":{\"channel\":{\"const\":\"primary\"},\"action\":{\"enum\":[\"extend\",\"retract\"]}}}"
#define STOP_SCHEMA "{\"type\":\"object\",\"required\":[\"channel\",\"action\"],\"properties\":{\"channel\":{\"const\":\"primary\"},\"action\":{\"const\":\"stop\"}}}"
static const ABoxMqttMatcher directions[]={EQ("data.params.action","extend"),EQ("data.params.action","retract")};
static const ABoxMqttMatcher move_terms[]={EQ("data.params.channel","primary"),
    {.kind=ABOX_MATCH_ANY,.children=directions,.count=COUNT(directions)}};
static const ABoxMqttMatcher stop_terms[]={EQ("data.params.channel","primary"),EQ("data.params.action","stop")};
static const ABoxMqttMatcher aux_terms[]={EQ("data.params.channel","auxiliary"),EQ("data.params.action","extend")};
static const ABoxMqttMatcher move={.kind=ABOX_MATCH_ALL,.children=move_terms,.count=COUNT(move_terms)};
static const ABoxMqttMatcher stop={.kind=ABOX_MATCH_ALL,.children=stop_terms,.count=COUNT(stop_terms)};
static const ABoxMqttMatcher aux_match={.kind=ABOX_MATCH_ALL,.children=aux_terms,.count=COUNT(aux_terms)};
static uint16_t object_params(void *context,const cJSON *params)
{ (void)context;return cJSON_IsObject(params) ? 200 : 4001; }
static uint16_t null_params(void *context,const cJSON *params)
{ (void)context;return cJSON_IsNull(params) ? 200 : 4001; }
static const uint16_t errors[]={4003,5001},majors[]={1};
static const ABoxMqttOperationContract operations[]={
    {{"actuator_control","fixture_actuator",ABOX_MQTT_COMMAND_DISCRETE_ACTION,ABOX_MQTT_EXPIRY_REQUIRED,5000,NULL,object_params,NULL},
        &move,"1.0",CONTROL_DESCRIPTION,MOVE_SCHEMA,NULL,"controller accepted direction","persistent_idempotency",errors,COUNT(errors)},
    {{"actuator_control","fixture_actuator",ABOX_MQTT_COMMAND_SAFETY_STOP,ABOX_MQTT_EXPIRY_FORBIDDEN,0,NULL,object_params,NULL},
        &stop,"1.0",CONTROL_DESCRIPTION,STOP_SCHEMA,NULL,"controller confirmed stopped","repeatable_stop",errors,COUNT(errors)},
    {{"query_actuator","fixture_actuator",ABOX_MQTT_COMMAND_QUERY,ABOX_MQTT_EXPIRY_FORBIDDEN,0,NULL,null_params,NULL},
        NULL,"1.0","Read the current actuator snapshot.","{\"type\":\"null\"}","{\"type\":\"object\"}","snapshot captured","repeatable_query",NULL,0}
};
static const ABoxMqttOperationContract aux_operations[]={
    {{"actuator_control","fixture_auxiliary",ABOX_MQTT_COMMAND_DISCRETE_ACTION,ABOX_MQTT_EXPIRY_REQUIRED,4000,NULL,object_params,NULL},
        &aux_match,"1.0",CONTROL_DESCRIPTION,"{\"type\":\"object\"}",NULL,"auxiliary accepted direction","persistent_idempotency",errors,COUNT(errors)}
};
static const ABoxMqttCommandDescriptor commands[]={
    {"actuator_control","fixture_actuator",ABOX_MQTT_COMMAND_PARAMETERIZED},
    {"query_actuator","fixture_actuator",ABOX_MQTT_COMMAND_QUERY}
};
static const ABoxMqttCommandDescriptor aux_commands[]={{"actuator_control","fixture_auxiliary",ABOX_MQTT_COMMAND_DISCRETE_ACTION}};
static const ABoxMqttReportDescriptor report_descriptors[]={
    {"actuator_status","fixture_actuator","state"},{"actuator_position","fixture_actuator","telemetry"}
};
static const ABoxMqttReportDescriptor aux_reports[]={{"auxiliary_status","fixture_auxiliary","state"}};
static const ABoxMqttReportContract report_contracts[]={
    {"actuator_status","1.0","Complete actuator state.","{\"type\":\"object\",\"properties\":{\"position\":{\"type\":\"number\",\"minimum\":0,\"multipleOf\":0.1}}}",1,0,1,1},
    {"actuator_position","1.0","Latest measured position.","{\"type\":\"object\"}",0,0,0,0}
};
static const ABoxMqttReportContract aux_report_contracts[]={{"auxiliary_status","1.0","Complete auxiliary state.","{\"type\":\"object\"}",1,1,0,1}};
static const ABoxMqttProfileDescriptor descriptor={"fixture_actuator","1.0",commands,COUNT(commands),report_descriptors,COUNT(report_descriptors)};
static const ABoxMqttProfileDescriptor aux_descriptor={"fixture_auxiliary","1.1",aux_commands,COUNT(aux_commands),aux_reports,COUNT(aux_reports)};
static const ABoxMqttProfileContract profile={&descriptor,"fixture://actuator",majors,COUNT(majors),operations,COUNT(operations),report_contracts,COUNT(report_contracts)};
static const ABoxMqttProfileContract auxiliary={&aux_descriptor,"fixture://auxiliary",majors,COUNT(majors),aux_operations,COUNT(aux_operations),aux_report_contracts,COUNT(aux_report_contracts)};
static const ABoxMqttProfileContract *const profiles[]={&profile,&auxiliary};
static const ABoxMqttRegistryContract registry={profiles,COUNT(profiles)};
static int sink(void *context,const char *bytes,size_t size)
{ (void)context;(void)bytes;(void)size;return 1; }
static int refuse(void *context,const char *bytes,size_t size)
{ (void)context;(void)bytes;(void)size;return 0; }
static int callback(void *context,const cJSON *params) { (void)context;(void)params;return 1; }
static void request(const char *channel,const char *action,uint16_t expected,const char *owner,ABoxMqttCommandClass category)
{
    char json[400];ABoxMqttRequest r={0};const ABoxMqttOperation *op=NULL;
    int n=snprintf(json,sizeof(json),"{\"version\":\"4.0\",\"timestamp\":1000,\"data\":{\"requestId\":\"r\",\"command\":\"actuator_control\",%s\"params\":{\"channel\":\"%s\",\"action\":\"%s\"}}}",
        !strcmp(action,"stop") ? "" : "\"expiresAt\":5000,",channel,action);
    assert(n>0 && (size_t)n<sizeof(json));
    assert(ABoxMqttRequest_Decode(&r,json,(size_t)n,NULL,NULL)==200);
    assert(ABoxMqttRegistry_Resolve(&registry,&r,&op)==expected);
    if (expected==200) assert(op && !strcmp(op->profile,owner) && op->classification==category);
    else assert(!op);
    ABoxMqttRequest_Close(&r);
}
int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--describe")) return ABoxMqttRegistry_Dump(&registry) ? 0 : 1;
    assert(ABoxMqttRegistry_Valid(&registry));assert(ABoxMqttRegistry_Describe(&registry,sink,NULL));
    assert(!ABoxMqttRegistry_Describe(&registry,refuse,NULL));
    request("primary","extend",200,"fixture_actuator",ABOX_MQTT_COMMAND_DISCRETE_ACTION);
    request("primary","retract",200,"fixture_actuator",ABOX_MQTT_COMMAND_DISCRETE_ACTION);
    request("primary","stop",200,"fixture_actuator",ABOX_MQTT_COMMAND_SAFETY_STOP);
    request("auxiliary","extend",200,"fixture_auxiliary",ABOX_MQTT_COMMAND_DISCRETE_ACTION);
    request("unknown","extend",4001,NULL,ABOX_MQTT_COMMAND_CORE);
    request("primary","unknown",4001,NULL,ABOX_MQTT_COMMAND_CORE);
    ABoxMqttOperationContract changed[4];memcpy(changed,operations,sizeof(operations));
    ABoxMqttProfileContract copy=profile;copy.operations=changed;
    const ABoxMqttProfileContract *selected[]={&copy,&auxiliary};ABoxMqttRegistryContract modified={selected,COUNT(selected)};
    changed[0].runtime.max_validity_ms=30001;assert(!ABoxMqttRegistry_Valid(&modified));changed[0]=operations[0];
    changed[0].runtime.matches=callback;assert(!ABoxMqttRegistry_Valid(&modified));changed[0]=operations[0];
    changed[0].introduced_version="2.0";assert(!ABoxMqttRegistry_Valid(&modified));changed[0]=operations[0];
    changed[0].params_schema="{broken";assert(!ABoxMqttRegistry_Describe(&modified,sink,NULL));changed[0]=operations[0];
    ABoxMqttCommandDescriptor wrong[2];memcpy(wrong,commands,sizeof(commands));wrong[0].classification=ABOX_MQTT_COMMAND_DISCRETE_ACTION;
    ABoxMqttProfileDescriptor bad_descriptor=descriptor;bad_descriptor.commands=wrong;copy.descriptor=&bad_descriptor;
    assert(!ABoxMqttRegistry_Valid(&modified));copy.descriptor=&descriptor;
    changed[3]=operations[0];copy.operation_count=4;
    const char *ambiguous="{\"version\":\"4.0\",\"timestamp\":1000,\"data\":{\"requestId\":\"r\",\"command\":\"actuator_control\",\"expiresAt\":5000,\"params\":{\"channel\":\"primary\",\"action\":\"extend\"}}}";
    ABoxMqttRequest r={0};const ABoxMqttOperation *op;
    assert(ABoxMqttRequest_Decode(&r,ambiguous,strlen(ambiguous),NULL,NULL)==200);
    assert(ABoxMqttRegistry_Resolve(&modified,&r,&op)==4001 && !op);ABoxMqttRequest_Close(&r);
    const char *probe="{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"p\",\"command\":\"query_actuator\",\"params\":{\"flag\":true,\"nullable\":null}}}";
    assert(ABoxMqttRequest_Decode(&r,probe,strlen(probe),NULL,NULL)==200);
    ABoxMqttMatcher m={.kind=ABOX_MATCH_EQUALS,.field="data.params.flag",.equals={.type=ABOX_MATCH_INTEGER,.value.integer=1}};
    assert(!ABoxMqttMatcher_Matches(&m,&r));m.equals.type=ABOX_MATCH_BOOLEAN;m.equals.value.boolean=1;assert(ABoxMqttMatcher_Matches(&m,&r));
    m.field="data.params.nullable";m.equals.type=ABOX_MATCH_NULL;assert(ABoxMqttMatcher_Matches(&m,&r));
    m.field="data.params.missing";assert(!ABoxMqttMatcher_Matches(&m,&r));
    assert(ABoxMqttRegistry_Resolve(&registry,&r,&op)==4001);ABoxMqttRequest_Close(&r);
    const char *sync="{\"version\":\"4.0\",\"timestamp\":0,\"data\":{\"requestId\":\"s\",\"command\":\"sync_state\",\"params\":null}}";
    assert(ABoxMqttRequest_Decode(&r,sync,strlen(sync),NULL,NULL)==200);
    assert(ABoxMqttRegistry_Resolve(&registry,&r,&op)==200 && op->classification==ABOX_MQTT_COMMAND_QUERY);ABoxMqttRequest_Close(&r);
    ABoxMqttMatcher cyclic={.kind=ABOX_MATCH_ALL,.count=1};cyclic.children=&cyclic;assert(!ABoxMqttMatcher_Valid(&cyclic));
    puts("PASS compiled descriptors, declarative parameter routing, expiry and report contracts");return 0;
}
