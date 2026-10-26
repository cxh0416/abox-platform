#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "abox_cloud_v4.h"
static unsigned nonce_sequence;
static int nonce(void *context, uint8_t output[16])
{ (void)context; memset(output,0,16); output[0]=(uint8_t)++nonce_sequence; return 1; }
int main(void)
{
    const char *instances[]={"2","1"};
    ABoxCloudV4Profile p[]={{"wing","1.0",NULL,0},{"refrigeration","1.2",instances,2}};
    ABoxCloudV4Profile reverse[]={p[1],p[0]};
    char out[1200],id[72],other[72];
    assert(ABoxCloudV4_ManifestId(id,"app-1",p,2));
    assert(ABoxCloudV4_ManifestId(other,"app-1",reverse,2) && !strcmp(id,other));
    assert(ABoxCloudV4_Manifest(out,sizeof(out),1770186088333ULL,"app-1",p,2,1));
    assert(strstr(out,"\"instances\":[\"1\",\"2\"]"));
    cJSON *root=cJSON_Parse(out), *data=cJSON_GetObjectItemCaseSensitive(root,"data");
    assert(root && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(data,"profiles"))==2);
    uint8_t digest[32]; char expected[72]="sha256:"; static const char digits[]="0123456789abcdef";
    cJSON_DeleteItemFromObjectCaseSensitive(data,"manifestId");
    assert(ABoxMqttDigest_Value(data,digest));
    for (unsigned i=0;i<32;++i) { expected[7+2*i]=digits[digest[i]>>4]; expected[8+2*i]=digits[digest[i]&15]; }
    expected[71]=0; assert(!strcmp(expected,id)); cJSON_Delete(root);
    reverse[1]=reverse[0]; assert(!ABoxCloudV4_ManifestId(other,"app-1",reverse,2));
    for (size_t cap=1;cap<80;++cap) { memset(out,0xa5,sizeof(out)); assert(!ABoxCloudV4_Manifest(out,cap,0,"app-1",p,2,1)); assert(!out[0]); }
    assert(ABoxCloudV4_Heartbeat(out,sizeof(out),0,"app-1",id));
    assert(strstr(out,id)); assert(!ABoxCloudV4_Heartbeat(out,sizeof(out),0,"app-1","sha256:bad"));
    ABoxCloudV4Capabilities capabilities={0};
    assert(ABoxCloudV4Capabilities_Set(&capabilities,"app-1",p,2,0));
    assert(!strcmp(capabilities.manifest_id,id));
    assert(ABoxCloudV4Capabilities_Heartbeat(&capabilities,out,sizeof(out),0) && strstr(out,id));
    assert(ABoxCloudV4Capabilities_Set(&capabilities,"app-1",p,1,0));
    assert(strcmp(capabilities.manifest_id,id) && capabilities.generation==2);
    assert(ABoxCloudV4Capabilities_Manifest(&capabilities,out,sizeof(out),0) && strstr(out,capabilities.manifest_id));
    cJSON *payload=cJSON_CreateObject(); cJSON_AddNumberToObject(payload,"positionMm",123);
    ABoxCloudV4Report report={"r1","state","wing_status",NULL,NULL,"request1",0,1770186088333ULL};
    assert(ABoxCloudV4_Report(out,sizeof(out),&report,payload));
    assert(strstr(out,"1770186088333") && strstr(out,"\"requestId\":\"request1\""));
    ABoxMqttReportDescriptor descriptor={"wing_status","wing","state"};
    assert(ABoxCloudV4_ReportRegistered(out,sizeof(out),&capabilities,&descriptor,&report,payload));
    ABoxCloudV4Profile empty_instances={"wing","1.0",instances,0};
    assert(ABoxCloudV4Capabilities_Set(&capabilities,"app-1",&empty_instances,1,0));
    assert(ABoxCloudV4Capabilities_Manifest(&capabilities,out,sizeof(out),0) && strstr(out,"\"instances\":[]"));
    assert(!ABoxCloudV4_ReportRegistered(out,sizeof(out),&capabilities,&descriptor,&report,payload));
    assert(ABoxCloudV4Capabilities_Set(&capabilities,"app-1",p,1,0));
    report.instance_id="1";assert(!ABoxCloudV4_ReportRegistered(out,sizeof(out),&capabilities,&descriptor,&report,payload));
    report.instance_id=NULL;
    report.stream_id="stream1"; assert(!ABoxCloudV4_Report(out,sizeof(out),&report,payload));
    report.sequence=1; assert(ABoxCloudV4_Report(out,sizeof(out),&report,payload));
    assert(ABoxCloudV4_Response(out,sizeof(out),0,"q",200,"OK",NULL)==200);
    assert(!strstr(out,"result"));
    char large[800]; memset(large,'x',sizeof(large)-1); large[sizeof(large)-1]=0;
    cJSON_AddStringToObject(payload,"large",large);
    assert(ABoxCloudV4_Response(out,sizeof(out),0,"q",200,"OK",payload)==500);
    assert(strstr(out,"\"code\":500")); cJSON_Delete(payload);
    ABoxCloudV4StateKey storage[3],keys[]={{"wing_status",NULL},{"temperature_status","1"}};
    ABoxCloudV4Sync sync; uint8_t request_digest[32]={1};
    assert(ABoxCloudV4Sync_Init(&sync,storage,3));
    assert(ABoxCloudV4Sync_Begin(&sync,"sync1",request_digest,keys,2)==200);
    assert(ABoxCloudV4Sync_Begin(&sync,"sync1",request_digest,NULL,0)==200 && sync.count==2);
    assert(ABoxCloudV4Sync_Begin(&sync,"sync2",request_digest,NULL,0)==4003 && sync.count==2);
    ++request_digest[0]; assert(ABoxCloudV4Sync_Begin(&sync,"sync1",request_digest,keys,2)==4006);
    assert(ABoxCloudV4Sync_Result(&sync,out,sizeof(out)) && strstr(out,"temperature_status"));
    ABoxCloudService service;ABoxCloudServiceOptions options={5000,0,0};ABoxCloudJob job;uint64_t operation=1;
    assert(ABoxCloudService_Init(&service,&options,0));ABoxCloudService_SetStateCount(&service,1);
    ABoxCloudService_SetReady(&service,1,0);
    while (ABoxCloudService_Bootstrapping(&service)) {
        assert(ABoxCloudService_Next(&service,0,&job));
        assert(ABoxCloudService_Begin(&service,&job,operation,0));
        ABoxCloudService_Receipt(&service,job.generation,operation++,1,0);
    }
    assert(ABoxCloudV4Sync_Queue(&sync,&service));
    assert(ABoxCloudService_Next(&service,0,&job) && job.kind==ABOX_CLOUD_JOB_RESPONSE);
    assert(ABoxCloudService_Begin(&service,&job,operation,0));
    ABoxCloudService_Receipt(&service,job.generation,operation++,1,0);
    ABoxCloudService_SetStateCount(&service,1); /* Live configuration shrank after acceptance. */
    for (unsigned index=0;index<2;++index) {
        assert(ABoxCloudService_Next(&service,0,&job) && job.kind==ABOX_CLOUD_JOB_STATE && job.state_index==index);
        assert(!strcmp(job.request_id,"sync1"));
        assert(ABoxCloudService_Begin(&service,&job,operation,0));
        ABoxCloudService_Receipt(&service,job.generation,operation++,1,0);
    }
    assert(!service.sync_pending); /* Both frozen keys, not the one live key. */
    ABoxCloudV4IdPort port={NULL,nonce}; ABoxCloudV4Ids ids; ABoxCloudV4Stream stream={0};
    char first[97],next[97];
    assert(ABoxCloudV4Ids_Init(&ids,&port)); assert(ABoxCloudV4Ids_Next(&ids,first));
    assert(ABoxCloudV4Ids_Next(&ids,next) && strcmp(first,next));
    assert(ABoxCloudV4Stream_Next(&stream,&ids) && stream.sequence==1);
    strcpy(first,stream.id); stream.sequence=UINT32_MAX;
    assert(ABoxCloudV4Stream_Next(&stream,&ids) && stream.sequence==1 && strcmp(first,stream.id));
    ids.counter=UINT64_MAX; assert(ABoxCloudV4Ids_Next(&ids,next));
    char packet_bytes[64]; ABoxCloudV4Packet packet={packet_bytes,sizeof(packet_bytes),0,0,0,0};
    assert(ABoxCloudV4Packet_Queue(&packet,"{\"id\":1}",8));
    assert(ABoxCloudV4Packet_Begin(&packet,1,100));
    assert(!ABoxCloudV4Packet_Receipt(&packet,1,101,ABOX_MQTT_RECEIPT_CONFIRMED));
    assert(!ABoxCloudV4Packet_Receipt(&packet,1,100,ABOX_MQTT_RECEIPT_FAILED) && packet.pending);
    assert(ABoxCloudV4Packet_Begin(&packet,1,102)); ABoxCloudV4Packet_Disconnected(&packet);
    assert(!ABoxCloudV4Packet_Receipt(&packet,1,102,ABOX_MQTT_RECEIPT_CONFIRMED));
    assert(!strcmp(packet_bytes,"{\"id\":1}") && ABoxCloudV4Packet_Begin(&packet,2,103));
    assert(ABoxCloudV4Packet_Receipt(&packet,2,103,ABOX_MQTT_RECEIPT_CONFIRMED) && !packet.pending);
    puts("PASS Manifest sets/digest, bounded envelopes, frozen sync keys, IDs/streams and immutable retries");
    return 0;
}
