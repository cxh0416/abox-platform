#include "abox_cloud_v4.h"
#include "abox_cloud_response.h"
#include "abox_sha256.h"
#include <string.h>
#include <stdio.h>

typedef struct { char *out; size_t capacity, used; int ok; } Writer;
static int bytes(void *context, const char *s, size_t n)
{
    Writer *w=context;
    if (!w->ok || n>=w->capacity-w->used) { w->ok=0; return 0; }
    memcpy(w->out+w->used,s,n); w->used+=n; w->out[w->used]=0; return 1;
}
static void raw(Writer *w, const char *s) { (void)bytes(w,s,strlen(s)); }
static void string(Writer *w, const char *s)
{ if (!ABoxMqttDigest_WriteString(s,bytes,w)) w->ok=0; }
static void integer(Writer *w, uint64_t n)
{
    char value[21];
    if (!ABoxMqttV4_FormatInteger(value,sizeof(value),n)) w->ok=0;
    else raw(w,value);
}
static int finish(Writer *w)
{ if (!w->ok && w->capacity) w->out[0]=0; return w->ok; }
static int init(Writer *w, char *out, size_t size, uint64_t timestamp)
{
    if (!out || !size || timestamp>9007199254740991ULL) return 0;
    w->out=out; w->capacity=size>1025U ? 1025U : size; w->used=0; w->ok=1; out[0]=0;
    raw(w,"{\"version\":\"4.0\",\"timestamp\":"); integer(w,timestamp); raw(w,",\"data\":{"); return 1;
}
static int firmware_valid(const char *s)
{
    size_t i;
    if (!s || !*s) return 0;
    for (i=0;s[i];++i) if (i>=64 || (unsigned char)s[i]<32 || (unsigned char)s[i]>126) return 0;
    return 1;
}
static int profiles_valid(const ABoxCloudV4Profile *p, size_t count)
{
    size_t i,j,k;
    if (count>16 || (count && !p)) return 0;
    for (i=0;i<count;++i) {
        if (!ABoxMqttV4_NameValid(p[i].name) || !ABoxMqttV4_ProfileVersionValid(p[i].version) ||
            (p[i].instance_count && !p[i].instances)) return 0;
        for (j=0;j<i;++j) if (!strcmp(p[i].name,p[j].name)) return 0;
        for (j=0;j<p[i].instance_count;++j) {
            if (!ABoxMqttV4_IdValid(p[i].instances[j],ABOX_MQTT_ID_INSTANCE)) return 0;
            for (k=0;k<j;++k) if (!strcmp(p[i].instances[j],p[i].instances[k])) return 0;
        }
    }
    return 1;
}
/* Selection scans avoid a second Profile/instance array in scarce RAM. Names
 * are ASCII per contract, so strcmp is the required Unicode codepoint order. */
static int canonical_manifest(const char *firmware, const ABoxCloudV4Profile *p,
    size_t count, ABoxCanonicalWriter write, void *context)
{
    size_t i,j,k; const char *previous=NULL;
#define EMIT(s) do { if (!write(context,s,sizeof(s)-1U)) return 0; } while (0)
    EMIT("{\"firmware\":");
    if (!ABoxMqttDigest_WriteString(firmware,write,context)) return 0;
    EMIT(",\"profiles\":[");
    for (i=0;i<count;++i) {
        const ABoxCloudV4Profile *chosen=NULL;
        for (j=0;j<count;++j) if ((!previous || strcmp(p[j].name,previous)>0) &&
            (!chosen || strcmp(p[j].name,chosen->name)<0)) chosen=&p[j];
        if (!chosen) return 0;
        if (i) EMIT(",");
        EMIT("{");
        if (chosen->instances) {
            const char *last=NULL;
            EMIT("\"instances\":[");
            for (j=0;j<chosen->instance_count;++j) {
                const char *id=NULL;
                for (k=0;k<chosen->instance_count;++k) if ((!last || strcmp(chosen->instances[k],last)>0) &&
                    (!id || strcmp(chosen->instances[k],id)<0)) id=chosen->instances[k];
                if (!id) return 0;
                if (j) EMIT(",");
                if (!ABoxMqttDigest_WriteString(id,write,context)) return 0;
                last=id;
            }
            EMIT("],");
        }
        EMIT("\"name\":"); if (!ABoxMqttDigest_WriteString(chosen->name,write,context)) return 0;
        EMIT(",\"version\":"); if (!ABoxMqttDigest_WriteString(chosen->version,write,context)) return 0;
        EMIT("}"); previous=chosen->name;
    }
    EMIT("]}"); return 1;
#undef EMIT
}
static int hash(void *context, const char *s, size_t n)
{ ABoxSha256_Update(context,(const uint8_t *)s,n); return 1; }
static void hex(char *out, const uint8_t *data, size_t n)
{
    static const char digits[]="0123456789abcdef"; size_t i;
    for (i=0;i<n;++i) { out[2*i]=digits[data[i]>>4]; out[2*i+1]=digits[data[i]&15U]; }
    out[2*n]=0;
}
int ABoxCloudV4_ManifestId(char out[72], const char *firmware, const ABoxCloudV4Profile *p, size_t count)
{
    ABoxSha256 sha; uint8_t digest[32];
    if (!out) return 0;
    out[0]=0;
    if (!firmware_valid(firmware) || !profiles_valid(p,count)) return 0;
    ABoxSha256_Init(&sha);
    if (!canonical_manifest(firmware,p,count,hash,&sha)) return 0;
    ABoxSha256_Final(&sha,digest); strcpy(out,"sha256:"); hex(out+7,digest,32); return 1;
}
int ABoxCloudV4_Manifest(char *out, size_t size, uint64_t ts, const char *firmware,
    const ABoxCloudV4Profile *p, size_t count, int with_id)
{
    Writer w; char id[72];
    if (out && size) out[0]=0;
    if (!firmware_valid(firmware) || !profiles_valid(p,count) || !init(&w,out,size,ts)) return 0;
    if (!w.ok) return finish(&w);
    /* Replace the empty data object prefix with canonical data directly. */
    --w.used; w.out[w.used]=0;
    if (!canonical_manifest(firmware,p,count,bytes,&w)) w.ok=0;
    if (with_id && w.ok) {
        if (!ABoxCloudV4_ManifestId(id,firmware,p,count)) w.ok=0;
        --w.used; w.out[w.used]=0;
        raw(&w,",\"manifestId\":"); string(&w,id); raw(&w,"}");
    }
    raw(&w,"}"); return finish(&w);
}
static int manifest_id_valid(const char *s)
{
    size_t i;
    if (!s || strlen(s)!=71 || strncmp(s,"sha256:",7)) return 0;
    for (i=7;i<71;++i) if (!((s[i]>='0' && s[i]<='9') || (s[i]>='a' && s[i]<='f'))) return 0;
    return 1;
}
int ABoxCloudV4_Heartbeat(char *out, size_t size, uint64_t ts, const char *firmware, const char *id)
{
    Writer w;
    if (out && size) out[0]=0;
    if (!firmware_valid(firmware) || (id && !manifest_id_valid(id)) || !init(&w,out,size,ts)) return 0;
    raw(&w,"\"firmware\":"); string(&w,firmware);
    if (id) { raw(&w,",\"manifestId\":"); string(&w,id); }
    raw(&w,"}}"); return finish(&w);
}
int ABoxCloudV4Capabilities_Set(ABoxCloudV4Capabilities *c, const char *firmware,
    const ABoxCloudV4Profile *p, size_t count, int fixed)
{
    char id[72];
    if (!c || !ABoxCloudV4_ManifestId(id,firmware,p,count)) return 0;
    c->firmware=firmware; c->profiles=p; c->count=count;
    if (fixed) c->manifest_id[0]=0; else strcpy(c->manifest_id,id);
    if (++c->generation==0) ++c->generation;
    return 1;
}
int ABoxCloudV4Capabilities_Manifest(const ABoxCloudV4Capabilities *c, char *out, size_t size, uint64_t ts)
{
    return c && c->generation && ABoxCloudV4_Manifest(out,size,ts,c->firmware,c->profiles,c->count,c->manifest_id[0]!=0);
}
int ABoxCloudV4Capabilities_Heartbeat(const ABoxCloudV4Capabilities *c, char *out, size_t size, uint64_t ts)
{
    return c && c->generation && ABoxCloudV4_Heartbeat(out,size,ts,c->firmware,c->manifest_id[0] ? c->manifest_id : NULL);
}
static int value(Writer *w, const cJSON *json)
{
    size_t start=w->used;
    if (!ABoxMqttDigest_Canonical(json,bytes,w) || w->used-start>768U) w->ok=0;
    return w->ok;
}
int ABoxCloudV4_Report(char *out, size_t size, const ABoxCloudV4Report *r, const cJSON *payload)
{
    Writer w;
    if (out && size) out[0]=0;
    if (!r || !ABoxMqttV4_IdValid(r->report_id,ABOX_MQTT_ID_REPORT) || !ABoxMqttV4_NameValid(r->name) ||
        !r->type || (strcmp(r->type,"state") && strcmp(r->type,"event") && strcmp(r->type,"telemetry")) ||
        (r->instance_id && !ABoxMqttV4_IdValid(r->instance_id,ABOX_MQTT_ID_INSTANCE)) ||
        (r->request_id && !ABoxMqttV4_IdValid(r->request_id,ABOX_MQTT_ID_REQUEST)) ||
        (r->stream_id ? (!r->sequence || !ABoxMqttV4_IdValid(r->stream_id,ABOX_MQTT_ID_STREAM)) : r->sequence!=0) ||
        (!cJSON_IsObject(payload) && !cJSON_IsNull(payload)) || !init(&w,out,size,r->timestamp)) return 0;
    raw(&w,"\"reportId\":"); string(&w,r->report_id);
    raw(&w,",\"reportType\":"); string(&w,r->type);
    raw(&w,",\"reportName\":"); string(&w,r->name);
    if (r->instance_id) { raw(&w,",\"instanceId\":"); string(&w,r->instance_id); }
    if (r->request_id) { raw(&w,",\"requestId\":"); string(&w,r->request_id); }
    if (r->stream_id) {
        raw(&w,",\"streamId\":"); string(&w,r->stream_id);
        raw(&w,",\"sequence\":"); integer(&w,r->sequence);
    }
    raw(&w,",\"payload\":"); value(&w,payload); raw(&w,"}}"); return finish(&w);
}
static int result_encoder(void *context, char *out, size_t size)
{
    Writer w={out,size,0,1};
    if (!size) return 0;
    out[0]=0;
    return value(&w,context) && finish(&w);
}
int ABoxCloudV4_ReportRegistered(char *out, size_t size, const ABoxCloudV4Capabilities *c,
    const ABoxMqttReportDescriptor *d, const ABoxCloudV4Report *r, const cJSON *payload)
{
    size_t i,j;
    if (out && size) out[0]=0;
    if (!c || !c->generation || !d || !d->profile || !d->name || !d->report_type ||
        !r || !r->name || !r->type || strcmp(d->name,r->name) || strcmp(d->report_type,r->type)) return 0;
    for (i=0;i<c->count;++i) if (!strcmp(c->profiles[i].name,d->profile)) {
        const ABoxCloudV4Profile *p=&c->profiles[i];
        if (!p->instances) return !r->instance_id && ABoxCloudV4_Report(out,size,r,payload);
        if (!r->instance_id) return 0;
        for (j=0;j<p->instance_count;++j) if (!strcmp(p->instances[j],r->instance_id))
            return ABoxCloudV4_Report(out,size,r,payload);
        return 0;
    }
    return 0;
}
static int discard(void *context, const char *bytes_value, size_t size)
{ (void)context; (void)bytes_value; (void)size; return 1; }
uint16_t ABoxCloudV4_Response(char *out, size_t size, uint64_t ts, const char *id,
    uint16_t code, const char *msg, const cJSON *result)
{
    char timestamp[21]; size_t i;
    if (out && size) out[0]=0;
    if (!out || !size || ts>9007199254740991ULL || !ABoxMqttV4_IdValid(id,ABOX_MQTT_ID_REQUEST) ||
        !code || !msg || strlen(msg)>128 || (result && !cJSON_IsObject(result))) return 0;
    for (i=0;msg[i];++i) if ((unsigned char)msg[i]<32 || (unsigned char)msg[i]==127) return 0;
    if (!ABoxMqttDigest_WriteString(msg,discard,NULL)) return 0;
    if (!ABoxMqttV4_FormatInteger(timestamp,sizeof(timestamp),ts)) return 0;
    if (size>1025U) size=1025U;
    if (ABoxCloudResponse_EncodeResult(out,size,"4.0",timestamp,NULL,id,code,msg,
        result ? result_encoder : NULL,(void *)result)) return code;
    return ABoxCloudResponse_EncodeResult(out,size,"4.0",timestamp,NULL,id,500,
        "response too large",NULL,NULL) ? 500U : 0U;
}
int ABoxCloudV4Sync_Init(ABoxCloudV4Sync *s, ABoxCloudV4StateKey *keys, size_t capacity)
{
    if (!s) return 0;
    memset(s,0,sizeof(*s));
    if (!keys || !capacity) return 0;
    s->keys=keys; s->capacity=capacity; return 1;
}
void ABoxCloudV4Sync_Clear(ABoxCloudV4Sync *s)
{ if (s) { s->valid=0; s->count=0; s->request_id[0]=0; } }
uint16_t ABoxCloudV4Sync_Begin(ABoxCloudV4Sync *s, const char *id, const uint8_t digest[32],
    const ABoxCloudV4StateKey *keys, size_t count)
{
    size_t i,j;
    if (!s || !s->keys || !s->capacity || !digest || !ABoxMqttV4_IdValid(id,ABOX_MQTT_ID_REQUEST)) return 4001;
    if (s->valid && !strcmp(s->request_id,id)) return memcmp(s->digest,digest,32) ? 4006U : 200U;
    if (s->valid) return 4003;
    if (count>s->capacity) return 500;
    if (count && !keys) return 4001;
    for (i=0;i<count;++i) {
        if (!ABoxMqttV4_NameValid(keys[i].report_name) ||
            (keys[i].instance_id && !ABoxMqttV4_IdValid(keys[i].instance_id,ABOX_MQTT_ID_INSTANCE))) return 4001;
        for (j=0;j<i;++j) if (!strcmp(keys[i].report_name,keys[j].report_name) &&
            ((!keys[i].instance_id && !keys[j].instance_id) || (keys[i].instance_id && keys[j].instance_id &&
                !strcmp(keys[i].instance_id,keys[j].instance_id)))) return 4001;
    }
    if (count) memmove(s->keys,keys,count*sizeof(*keys));
    s->count=count;
    strcpy(s->request_id,id); memcpy(s->digest,digest,32); s->valid=1; return 200;
}
int ABoxCloudV4Sync_Result(const ABoxCloudV4Sync *s, char *out, size_t size)
{
    size_t i; Writer w={out,size>769U ? 769U : size,0,1};
    if (!s || !s->valid || !out || !size) return 0;
    out[0]=0; raw(&w,"{\"expectedStates\":[");
    for (i=0;i<s->count;++i) {
        if (i) raw(&w,",");
        raw(&w,"{\"reportName\":"); string(&w,s->keys[i].report_name);
        if (s->keys[i].instance_id) { raw(&w,",\"instanceId\":"); string(&w,s->keys[i].instance_id); }
        raw(&w,"}");
    }
    raw(&w,"]}"); return finish(&w);
}
int ABoxCloudV4Sync_Queue(const ABoxCloudV4Sync *s, ABoxCloudService *service)
{
    return s && s->valid && s->count<=UINT8_MAX &&
        ABoxCloudService_QueueSyncPlan(service,s->request_id,(uint8_t)s->count);
}
int ABoxCloudV4Ids_Init(ABoxCloudV4Ids *ids, const ABoxCloudV4IdPort *p)
{
    if (!ids) return 0;
    memset(ids,0,sizeof(*ids));
    if (!p || !p->nonce) return 0;
    ids->port=*p;
    ids->valid=p->nonce(p->context,ids->nonce)!=0; return ids->valid;
}
int ABoxCloudV4Ids_Next(ABoxCloudV4Ids *ids, char out[97])
{
    char number[21];
    if (!ids || !out || !ids->valid) return 0;
    if (ids->counter==UINT64_MAX) {
        uint8_t nonce[16];
        if (!ids->port.nonce(ids->port.context,nonce) || !memcmp(nonce,ids->nonce,16)) { ids->valid=0; return 0; }
        memcpy(ids->nonce,nonce,16); ids->counter=0;
    }
    ABoxMqttV4_FormatInteger(number,sizeof(number),++ids->counter);
    hex(out,ids->nonce,16); out[32]='-'; strcpy(out+33,number); return 1;
}
int ABoxCloudV4Stream_Next(ABoxCloudV4Stream *s, ABoxCloudV4Ids *ids)
{
    char id[97];
    if (!s || !ids) return 0;
    if (!s->sequence || s->sequence==UINT32_MAX) {
        if (!ABoxCloudV4Ids_Next(ids,id)) return 0;
        strcpy(s->id,id); s->sequence=0;
    }
    ++s->sequence; return 1;
}
int ABoxCloudV4Packet_Queue(ABoxCloudV4Packet *p, const char *data, size_t size)
{
    if (!p || !p->bytes || !data || p->pending || !size || size>1024 || size>=p->capacity || memchr(data,0,size)) return 0;
    memmove(p->bytes,data,size); p->bytes[size]=0; p->length=size; p->pending=1; p->operation=0; return 1;
}
int ABoxCloudV4Packet_Begin(ABoxCloudV4Packet *p, uint32_t generation, uint64_t op)
{
    if (!p || !p->pending || p->operation || !op || !generation) return 0;
    p->generation=generation; p->operation=op; return 1;
}
int ABoxCloudV4Packet_Receipt(ABoxCloudV4Packet *p, uint32_t generation, uint64_t op, ABoxMqttReceiptStatus status)
{
    if (!p || !p->pending || !op || p->operation!=op || p->generation!=generation || status==ABOX_MQTT_RECEIPT_PENDING) return 0;
    p->operation=0;
    if (status==ABOX_MQTT_RECEIPT_CONFIRMED) { p->pending=0; return 1; }
    return 0;
}
void ABoxCloudV4Packet_Disconnected(ABoxCloudV4Packet *p)
{ if (p) { p->operation=0; p->generation=0; } }
