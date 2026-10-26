#include "abox_ufs_journal.h"
#include "abox_boot_v2.h"
#include <string.h>
#include <limits.h>

#define HEADER 16U
#define OVERHEAD (HEADER + 4U)
#define MAGIC 0x314A3441UL /* A4J1, little endian */
enum { UNLOADED, LOAD, WRITE, VERIFY, READY, FAILED };
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t n)
{ p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24); }
static void release(ABoxUfsJournal *j)
{ if (j->leased) j->port.release(j->port.context); j->leased=0; }
static void fail(ABoxUfsJournal *j)
{ j->state=FAILED; release(j); }
static int valid(const ABoxUfsJournal *j)
{
    return get32(j->buffer)==MAGIC && get32(j->buffer+4)==j->size &&
        get32(j->buffer+12)<=j->payload_capacity &&
        get32(j->buffer+j->size-4)==ABoxBootV2_Crc32(j->buffer,(uint32_t)j->size-4U);
}
static int exact_read(const ABoxUfsJournal *j)
{
    /* Request one byte beyond the wire image. Only known EOF at exactly the
     * expected size proves this is a complete file, rather than a valid prefix. */
    return j->io.status==ABOX_ASYNC_TRUNCATED && j->io.transferred==j->size && valid(j);
}
static int encode(ABoxUfsJournal *j)
{
    size_t length=0;
    if (j->sequence==UINT32_MAX) return 0;
    memset(j->buffer,0,j->size);
    if (!j->port.encode(j->port.context,j->buffer+HEADER,j->payload_capacity,&length) ||
        length>j->payload_capacity) return 0;
    put32(j->buffer,MAGIC); put32(j->buffer+4,(uint32_t)j->size);
    put32(j->buffer+8,++j->sequence); put32(j->buffer+12,(uint32_t)length);
    j->expected_crc=ABoxBootV2_Crc32(j->buffer,(uint32_t)j->size-4U);
    put32(j->buffer+j->size-4,j->expected_crc);
    return 1;
}
int ABoxUfsJournal_Init(ABoxUfsJournal *j, const ABoxEc800UfsPort *files,
    const ABoxUfsJournalPort *p, const char *a, const char *b, uint8_t *buffer, size_t size)
{
    if (!j) return 0;
    memset(j,0,sizeof(*j));
    if (!p || !p->decode || !p->encode || !p->initialized || !p->mark_initialized ||
        !p->acquire || !p->release || !buffer || size<=OVERHEAD+1U || size>UINT32_MAX ||
        !ABoxEc800Ufs_PathValid(a) || !ABoxEc800Ufs_PathValid(b) || !strcmp(a,b)) return 0;
    j->port=*p; j->paths[0]=a; j->paths[1]=b;
    j->buffer=buffer; j->size=size-1U; j->payload_capacity=j->size-OVERHEAD;
    return ABoxEc800Ufs_Init(&j->io,files);
}
int ABoxUfsJournal_Load(ABoxUfsJournal *j, uint32_t now)
{
    if (!j || !j->io.port.start || j->state!=UNLOADED || !j->port.acquire(j->port.context)) return 0;
    j->leased=1; j->started=now; j->sequence=j->crc=0;
    j->slot=j->found=j->absent=j->submitted=0; j->current=1; j->state=LOAD; return 1;
}
int ABoxUfsJournal_Save(ABoxUfsJournal *j, uint32_t now)
{
    if (!j || j->state!=READY || !j->port.acquire(j->port.context)) return 0;
    j->leased=1; j->started=now; j->copies=2; j->slot=j->current^1U; j->submitted=0;
    if (!encode(j)) { fail(j); return -1; }
    j->state=WRITE; return 1;
}
int ABoxUfsJournal_Status(const ABoxUfsJournal *j)
{ return !j || !j->io.port.start || j->state==FAILED ? -1 : j->state==READY ? 1 : 0; }
void ABoxUfsJournal_OnModemReset(ABoxUfsJournal *j)
{
    if (!j) return;
    ABoxEc800Ufs_OnModemReset(&j->io); release(j);
    if (j->state!=FAILED) j->state=UNLOADED;
    j->submitted=0;
}
void ABoxUfsJournal_Poll(ABoxUfsJournal *j, uint32_t now)
{
    ABoxUfsRequest r;
    if (!j || j->state==UNLOADED || j->state==READY || j->state==FAILED) return;
    if ((uint32_t)(now-j->started)>=60000U) {
        if (j->submitted) ABoxEc800Ufs_Cancel(&j->io);
        fail(j); return;
    }
    if (j->submitted) {
        ABoxEc800Ufs_Poll(&j->io,now);
        if (j->io.status==ABOX_ASYNC_PENDING) return;
        j->submitted=0;
        if (j->state==LOAD) {
            if (j->io.status==ABOX_ASYNC_NOT_FOUND) ++j->absent;
            else if (j->io.status!=ABOX_ASYNC_OK && j->io.status!=ABOX_ASYNC_TRUNCATED) { fail(j); return; }
            else if (exact_read(j)) {
                uint32_t sequence=get32(j->buffer+8), crc=get32(j->buffer+j->size-4U);
                if (j->found && ((sequence==j->sequence && crc!=j->crc) ||
                    sequence-j->sequence==0x80000000UL)) { fail(j); return; }
                if (!j->found || (int32_t)(sequence-j->sequence)>0) {
                    if (!j->port.decode(j->port.context,j->buffer+HEADER,get32(j->buffer+12))) { fail(j); return; }
                    j->found=1; j->sequence=sequence; j->crc=crc; j->current=j->slot;
                }
            }
            if (!j->slot) { j->slot=1; return; }
            if (!j->found && (j->absent!=2 || j->port.initialized(j->port.context)!=0 ||
                !j->port.decode(j->port.context,NULL,0))) { fail(j); return; }
            /* Mirror the recovered set before actions can start. A later loss
             * of one file must not silently remove a protected reservation. */
            j->copies=2; j->slot=j->current^1U;
            if (!encode(j)) { fail(j); return; }
            j->state=WRITE; return;
        }
        if (j->state==WRITE) {
            if (j->io.status!=ABOX_ASYNC_OK) { fail(j); return; }
            j->state=VERIFY; return;
        }
        if (!exact_read(j) || get32(j->buffer+8)!=j->sequence ||
            get32(j->buffer+j->size-4U)!=j->expected_crc) { fail(j); return; }
        j->current=j->slot;
        if (--j->copies) {
            j->slot^=1U;
            if (!encode(j)) { fail(j); return; }
            j->state=WRITE; return;
        }
        if (!j->port.mark_initialized(j->port.context)) { fail(j); return; }
        j->state=READY; release(j); return;
    }
    r.operation=j->state==WRITE ? ABOX_UFS_UPLOAD : ABOX_UFS_READ;
    r.path=j->paths[j->slot]; r.data=j->buffer;
    r.size=(uint32_t)j->size+(j->state==WRITE ? 0U : 1U);
    if (ABoxEc800Ufs_Start(&j->io,&r,now,15000U)) j->submitted=1;
}
