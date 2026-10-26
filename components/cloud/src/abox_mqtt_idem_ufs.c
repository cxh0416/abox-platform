#include "abox_mqtt_idem_ufs.h"
#include <string.h>
static int decode(void *context, const uint8_t *data, size_t size)
{
    ABoxMqttIdemUfs *s=context;
    return ABoxMqttIdem_Restore(&s->cache,data,size,s->mono);
}
static int encode(void *context, uint8_t *data, size_t cap, size_t *size)
{ return ABoxMqttIdem_Encode(&((ABoxMqttIdemUfs *)context)->cache,data,cap,size); }
static int initialized(void *context)
{ ABoxMqttIdemUfs *s=context; return s->port.initialized(s->port.context); }
static int mark(void *context)
{ ABoxMqttIdemUfs *s=context; return s->port.mark_initialized(s->port.context); }
static int acquire(void *context)
{ ABoxMqttIdemUfs *s=context; return s->port.acquire(s->port.context); }
static void release(void *context)
{ ABoxMqttIdemUfs *s=context; s->port.release(s->port.context); }
static int commit(void *context)
{ ABoxMqttIdemUfs *s=context; return ABoxUfsJournal_Save(&s->journal,(uint32_t)s->mono); }
static int poll(void *context)
{ return ABoxUfsJournal_Status(&((ABoxMqttIdemUfs *)context)->journal); }
static int loaded(void *context)
{ return ((ABoxMqttIdemUfs *)context)->loaded; }
int ABoxMqttIdemUfs_Init(ABoxMqttIdemUfs *s, const char *id,
    ABoxMqttIdemEntry *entries, size_t count, uint8_t *responses, size_t response_capacity,
    const ABoxEc800UfsPort *files, const ABoxMqttIdemUfsPort *p,
    const char *a, const char *b, uint8_t *buffer, size_t size)
{
    ABoxUfsJournalPort journal={s,decode,encode,initialized,mark,acquire,release};
    ABoxMqttIdemPort cache={s,commit,poll,loaded};
    /* Worst-case size is fixed and checked at construction, before any
     * reservation or side effect can outgrow the borrowed journal buffer. */
    if (!s) return 0;
    memset(s,0,sizeof(*s));
    if (!p || !p->initialized || !p->mark_initialized || !p->acquire || !p->release ||
        response_capacity>1025U || count>(SIZE_MAX-60U)/(108U+response_capacity) ||
        size<60U+count*(108U+response_capacity)) return 0;
    s->port=*p;
    return ABoxMqttIdem_Init(&s->cache,id,entries,count,responses,response_capacity,&cache) &&
        ABoxUfsJournal_Init(&s->journal,files,&journal,a,b,buffer,size);
}
int ABoxMqttIdemUfs_Load(ABoxMqttIdemUfs *s, uint64_t mono)
{
    if (!s || s->loading || s->loaded) return 0;
    s->mono=mono;
    if (!ABoxUfsJournal_Load(&s->journal,(uint32_t)mono)) return 0;
    s->loading=1; return 1;
}
void ABoxMqttIdemUfs_Poll(ABoxMqttIdemUfs *s, uint64_t mono)
{
    int status;
    if (!s) return;
    s->mono=mono; ABoxUfsJournal_Poll(&s->journal,(uint32_t)mono);
    status=ABoxUfsJournal_Status(&s->journal);
    if (s->loading && status) { s->loading=0; s->loaded=status>0; }
    if (status<0) ABoxMqttIdem_Invalidate(&s->cache);
    else if (s->loaded) ABoxMqttIdem_Poll(&s->cache);
}
void ABoxMqttIdemUfs_OnModemReset(ABoxMqttIdemUfs *s)
{
    int failed;
    if (!s) return;
    failed=ABoxUfsJournal_Status(&s->journal)<0;
    ABoxMqttIdem_Invalidate(&s->cache); ABoxUfsJournal_OnModemReset(&s->journal);
    s->loading=0;
    if (!failed) s->loaded=0;
}
