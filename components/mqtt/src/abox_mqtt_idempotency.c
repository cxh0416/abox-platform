#include "abox_mqtt_idempotency.h"
#include "abox_mqtt_request.h"
#include <string.h>

static void put(uint8_t *p, uint64_t n, unsigned bytes)
{ unsigned i; for (i=0;i<bytes;++i) p[i]=(uint8_t)(n>>(8U*i)); }
static uint64_t get(const uint8_t *p, unsigned bytes)
{ unsigned i; uint64_t n=0; for (i=0;i<bytes;++i) n|=(uint64_t)p[i]<<(8U*i); return n; }
int ABoxMqttIdem_Init(ABoxMqttIdempotency *c, const char *id,
    ABoxMqttIdemEntry *entries, size_t count, uint8_t *responses,
    size_t response_capacity, const ABoxMqttIdemPort *port)
{
    if (!c) return 0;
    memset(c,0,sizeof(*c));
    if (!ABoxMqttV4_IdValid(id,ABOX_MQTT_ID_DEVICE) || !entries || !responses ||
        !count || count>UINT16_MAX || !response_capacity || response_capacity>1025U ||
        count>SIZE_MAX/response_capacity || !port || !port->commit || !port->poll || !port->loaded) return 0;
    c->entries=entries; c->count=count; c->responses=responses;
    c->response_capacity=response_capacity; c->device_id=id; c->port=*port; c->active=count;
    memset(entries,0,count*sizeof(*entries)); memset(responses,0,count*response_capacity); return 1;
}
int ABoxMqttIdem_Restore(ABoxMqttIdempotency *c, const uint8_t *p, size_t size, uint64_t mono)
{
    size_t i,j,offset=39;
    if (!c || !c->count || !c->entries || !c->device_id || c->active<c->count) return 0;
    c->ready=0; c->failed=1;
    memset(c->entries,0,c->count*sizeof(*c->entries));
    if (!size && !p) { c->failed=0; c->ready=1; return 1; }
    if (!p || size<39 || memcmp(p,"M4I1",4) || get(p+4,2)!=c->count ||
        !memchr(p+6,0,33) || strcmp((const char *)p+6,c->device_id)) return 0;
    for (i=0;i<c->count;++i) {
        ABoxMqttIdemEntry *e=&c->entries[i];
        if (offset>=size) return 0;
        e->state=p[offset++];
        if (!e->state) continue;
        if ((e->state!=ABOX_IDEM_PREPARED && e->state!=ABOX_IDEM_FINAL) || size-offset<107U) return 0;
        memcpy(e->request_id,p+offset,65); offset+=65;
        memcpy(e->digest,p+offset,32); offset+=32;
        e->expires_at=get(p+offset,8); offset+=8;
        e->response_length=(uint16_t)get(p+offset,2); offset+=2;
        if (!memchr(e->request_id,0,65) || !ABoxMqttV4_IdValid(e->request_id,ABOX_MQTT_ID_REQUEST) ||
            !e->expires_at || e->expires_at>9007199254740991ULL || e->response_length>=c->response_capacity ||
            e->response_length>size-offset ||
            memchr(p+offset,0,e->response_length) ||
            (e->state==ABOX_IDEM_FINAL ? !e->response_length : e->response_length!=0)) return 0;
        for (j=0;j<i;++j) if (c->entries[j].state && !strcmp(c->entries[j].request_id,e->request_id)) return 0;
        memcpy(c->responses+i*c->response_capacity,p+offset,e->response_length);
        c->responses[i*c->response_capacity+e->response_length]=0; offset+=e->response_length;
        e->protect_mono=mono;
        if (e->state==ABOX_IDEM_PREPARED) e->state=ABOX_IDEM_INTERRUPTED;
    }
    if (offset!=size) return 0;
    c->failed=0; c->ready=1; return 1;
}
int ABoxMqttIdem_Encode(const ABoxMqttIdempotency *c, uint8_t *out, size_t cap, size_t *length)
{
    size_t i,used=39;
    if (!c || !c->count || !c->entries || !c->device_id || !out || !length || cap<39) return 0;
    memcpy(out,"M4I1",4); put(out+4,c->count,2); memset(out+6,0,33); strcpy((char *)out+6,c->device_id);
    for (i=0;i<c->count;++i) {
        const ABoxMqttIdemEntry *e=&c->entries[i];
        uint8_t final=e->state==ABOX_IDEM_FINAL || e->state==ABOX_IDEM_FINALIZING;
        size_t n=final ? e->response_length : 0;
        if (cap-used<1U+(e->state ? 107U+n : 0U)) return 0;
        out[used++]=e->state ? (final ? ABOX_IDEM_FINAL : ABOX_IDEM_PREPARED) : ABOX_IDEM_EMPTY;
        if (!e->state) continue;
        memcpy(out+used,e->request_id,65); used+=65;
        memcpy(out+used,e->digest,32); used+=32;
        put(out+used,e->expires_at,8); used+=8; put(out+used,n,2); used+=2;
        memcpy(out+used,c->responses+i*c->response_capacity,n); used+=n;
    }
    *length=used; return 1;
}
ABoxMqttIdemMatch ABoxMqttIdem_Find(const ABoxMqttIdempotency *c,
    const char *id, const uint8_t digest[32], size_t *index)
{
    size_t i;
    if (!c || !id || !digest || !c->ready || !c->port.loaded(c->port.context)) return ABOX_IDEM_UNAVAILABLE;
    for (i=0;i<c->count;++i) {
        const ABoxMqttIdemEntry *e=&c->entries[i];
        if (!e->state || strcmp(e->request_id,id)) continue;
        if (index) *index=i;
        if (memcmp(e->digest,digest,32)) return ABOX_IDEM_CONFLICT;
        if (e->state==ABOX_IDEM_FINAL) return ABOX_IDEM_REPLAY;
        return e->state==ABOX_IDEM_INTERRUPTED || c->failed ? ABOX_IDEM_UNCERTAIN : ABOX_IDEM_WAIT;
    }
    return c->ready && !c->failed ? ABOX_IDEM_NEW : ABOX_IDEM_UNAVAILABLE;
}
uint16_t ABoxMqttIdem_Begin(ABoxMqttIdempotency *c, const char *id, const uint8_t digest[32],
    uint64_t expires, const ABoxClockSnapshot *clock, size_t *index)
{
    size_t i; uint16_t code; ABoxMqttIdemMatch match;
    if (!c || !index || !ABoxMqttV4_IdValid(id,ABOX_MQTT_ID_REQUEST) || !digest ||
        !expires || expires>9007199254740991ULL) return 4001;
    match=ABoxMqttIdem_Find(c,id,digest,index);
    if (match!=ABOX_IDEM_NEW) return match==ABOX_IDEM_CONFLICT ? 4006 : 4003;
    if (c->active<c->count) return 4003;
    code=ABoxMqttV4_CheckExpiry(clock,expires); if (code!=200) return code;
    for (i=0;i<c->count;++i) {
        ABoxMqttIdemEntry *e=&c->entries[i];
        if (!e->state || ((e->state==ABOX_IDEM_FINAL || e->state==ABOX_IDEM_INTERRUPTED) &&
            clock->mono_ms>=e->protect_mono && clock->utc_ms>=clock->error_ms &&
            clock->utc_ms-clock->error_ms>ABoxMqttV4_IdempotencyProtectUntil(e->expires_at))) {
            uint64_t until=ABoxMqttV4_IdempotencyProtectUntil(expires);
            uint64_t lower=clock->utc_ms>clock->error_ms ? clock->utc_ms-clock->error_ms : 0;
            uint64_t remaining=until>lower ? until-lower : 0;
            memset(e,0,sizeof(*e)); strcpy(e->request_id,id); memcpy(e->digest,digest,32);
            e->expires_at=expires; e->state=ABOX_IDEM_PREPARING;
            e->protect_mono=UINT64_MAX-clock->mono_ms<remaining ? UINT64_MAX : clock->mono_ms+remaining;
            c->active=i; c->committing=0; *index=i; return 200;
        }
    }
    return 4003;
}
void ABoxMqttIdem_Invalidate(ABoxMqttIdempotency *c)
{
    size_t i;
    if (!c) return;
    c->failed=1; c->active=c->count; c->committing=0;
    for (i=0;i<c->count;++i) if (c->entries[i].state && c->entries[i].state!=ABOX_IDEM_FINAL)
        c->entries[i].state=ABOX_IDEM_INTERRUPTED;
}
void ABoxMqttIdem_Poll(ABoxMqttIdempotency *c)
{
    int result;
    if (!c || c->failed || c->active>=c->count) return;
    result=c->committing ? c->port.poll(c->port.context) : c->port.commit(c->port.context);
    if (result<0) { ABoxMqttIdem_Invalidate(c); return; }
    if (!c->committing) { if (result>0) c->committing=1; return; }
    if (result>0) {
        ABoxMqttIdemEntry *e=&c->entries[c->active];
        e->state=e->state==ABOX_IDEM_PREPARING ? ABOX_IDEM_PREPARED : ABOX_IDEM_FINAL;
        c->active=c->count; c->committing=0;
    }
}
uint16_t ABoxMqttIdem_TakeExecutable(ABoxMqttIdempotency *c, size_t i, const ABoxClockSnapshot *clock)
{
    uint16_t code;
    if (!c || !c->ready || c->failed || !c->port.loaded(c->port.context) ||
        i>=c->count || c->entries[i].state!=ABOX_IDEM_PREPARED) return 4003;
    code=ABoxMqttV4_CheckExpiry(clock,c->entries[i].expires_at);
    if (code==200) c->entries[i].state=ABOX_IDEM_DISPATCHED;
    return code;
}
int ABoxMqttIdem_Complete(ABoxMqttIdempotency *c, size_t i, const char *response, size_t size)
{
    if (!c || !c->ready || c->failed || i>=c->count || c->active<c->count || !response ||
        !size || size>=c->response_capacity || memchr(response,0,size) ||
        (c->entries[i].state!=ABOX_IDEM_DISPATCHED && c->entries[i].state!=ABOX_IDEM_PREPARED)) return 0;
    memcpy(c->responses+i*c->response_capacity,response,size);
    c->responses[i*c->response_capacity+size]=0;
    c->entries[i].response_length=(uint16_t)size; c->entries[i].state=ABOX_IDEM_FINALIZING;
    c->active=i; c->committing=0; return 1;
}
const char *ABoxMqttIdem_Response(const ABoxMqttIdempotency *c, size_t i, size_t *length)
{
    if (!c || !c->ready || !c->port.loaded(c->port.context) ||
        i>=c->count || c->entries[i].state!=ABOX_IDEM_FINAL) return NULL;
    if (length) *length=c->entries[i].response_length;
    return (const char *)c->responses+i*c->response_capacity;
}
