#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "abox_mqtt_idem_ufs.h"

#define FILE_SIZE 600U
typedef struct { uint8_t bytes[FILE_SIZE+1U]; size_t size; int exists; } File;
typedef struct {
    File files[2]; ABoxUfsRequest request;
    uint64_t operation;
    unsigned writes, cut_write;
    size_t prefix;
    int initialized, leased, busy, io_error, pending;
} Disk;
typedef struct {
    Disk disk;
    ABoxMqttIdemUfs store;
    ABoxMqttIdemEntry entries[2];
    uint8_t responses[2*128],workspace[FILE_SIZE+1U];
    uint64_t mono;
    unsigned effects;
} Fixture;
static const char response[]="{\"version\":\"4.0\",\"timestamp\":1234,\"data\":{\"requestId\":\"on1\",\"code\":200,\"msg\":\"OK\"}}";
static uint8_t digest[32]={7};
static int start(void *context,const ABoxUfsRequest *r,uint64_t *operation)
{
    Disk *d=context;
    assert(!d->pending); d->request=*r; d->pending=1; *operation=++d->operation; return 1;
}
static ABoxAsyncStatus poll(void *context,uint64_t operation,uint32_t *transferred)
{
    Disk *d=context; assert(operation==d->operation && d->pending); d->pending=0;
    File *f=&d->files[strstr(d->request.path,"-b") ? 1 : 0];
    if (d->io_error) return ABOX_ASYNC_ERROR;
    if (d->request.operation==ABOX_UFS_UPLOAD) {
        size_t size=d->request.size;
        if (++d->writes==d->cut_write) size=d->prefix;
        assert(size<=FILE_SIZE); f->exists=1; f->size=size;
        memcpy(f->bytes,d->request.data,size); *transferred=(uint32_t)size;
        return size==d->request.size ? ABOX_ASYNC_OK : ABOX_ASYNC_ERROR;
    }
    if (!f->exists) return ABOX_ASYNC_NOT_FOUND;
    size_t size=f->size<d->request.size ? f->size : d->request.size;
    memcpy(d->request.data,f->bytes,size); *transferred=(uint32_t)size;
    return size==d->request.size ? ABOX_ASYNC_OK : ABOX_ASYNC_TRUNCATED;
}
static int cancel(void *context,uint64_t operation)
{ Disk *d=context; assert(operation==d->operation);d->pending=0;return 1; }
static int initialized(void *context) { return ((Fixture *)context)->disk.initialized; }
static int mark(void *context) { ((Fixture *)context)->disk.initialized=1;return 1; }
static int acquire(void *context)
{ Fixture *f=context; if (f->disk.busy || f->disk.leased) return 0; f->disk.leased=1;return 1; }
static void release(void *context)
{ Fixture *f=context; assert(f->disk.leased);f->disk.leased=0; }
static void boot(Fixture *f)
{
    ABoxEc800UfsPort files={&f->disk,start,poll,cancel};
    ABoxMqttIdemUfsPort resources={f,initialized,mark,acquire,release};
    f->disk.pending=f->disk.leased=0;
    assert(ABoxMqttIdemUfs_Init(&f->store,"device",f->entries,2,f->responses,128,
        &files,&resources,"UFS:idem-a.bin","UFS:idem-b.bin",f->workspace,sizeof(f->workspace)));
    assert(ABoxMqttIdemUfs_Load(&f->store,f->mono));
}
static void tick(Fixture *f) { ABoxMqttIdemUfs_Poll(&f->store,++f->mono); }
static void settled(Fixture *f)
{
    for (unsigned i=0;i<200 && (!f->store.loaded || f->store.cache.active<2);++i) {
        tick(f); if (ABoxUfsJournal_Status(&f->store.journal)<0) return;
    }
    assert(f->store.loaded && f->store.cache.active==2 && !f->disk.leased);
}
static ABoxClockSnapshot clock_now(Fixture *f)
{
    ABoxClockSnapshot clock={0};clock.quality=ABOX_CLOCK_SYNCHRONIZED;
    clock.utc_ms=1770186088333ULL+f->mono;clock.mono_ms=f->mono;clock.error_ms=1;return clock;
}
static void begin(Fixture *f,const char *id,size_t *index)
{
    ABoxClockSnapshot clock=clock_now(f);
    assert(ABoxMqttIdem_Begin(&f->store.cache,id,digest,clock.utc_ms+5000,&clock,index)==200);
    assert(ABoxMqttIdem_TakeExecutable(&f->store.cache,*index,&clock)==4003);
}
static void act(Fixture *f,size_t index)
{
    ABoxClockSnapshot clock=clock_now(f);
    assert(ABoxMqttIdem_TakeExecutable(&f->store.cache,index,&clock)==200);++f->effects;
    assert(ABoxMqttIdem_TakeExecutable(&f->store.cache,index,&clock)==4003);
    assert(ABoxMqttIdem_Complete(&f->store.cache,index,response,strlen(response)));
    assert(!ABoxMqttIdem_Response(&f->store.cache,index,NULL));
}
static void basic(void)
{
    Fixture f={0};size_t index,other;boot(&f);
    assert(ABoxMqttIdem_Find(&f.store.cache,"on1",digest,&index)==ABOX_IDEM_UNAVAILABLE);
    settled(&f); assert(f.disk.initialized && f.disk.files[0].exists && f.disk.files[1].exists);
    begin(&f,"on1",&index);settled(&f);act(&f,index);settled(&f);
    assert(!strcmp(ABoxMqttIdem_Response(&f.store.cache,index,NULL),response));
    uint8_t snapshot[FILE_SIZE];size_t size;
    assert(ABoxMqttIdem_Encode(&f.store.cache,snapshot,sizeof(snapshot),&size));
    snapshot[size-1U]=255; /* Malformed second entry must hide the valid first prefix. */
    assert(!ABoxMqttIdem_Restore(&f.store.cache,snapshot,size,f.mono));
    assert(ABoxMqttIdem_Find(&f.store.cache,"on1",digest,&index)==ABOX_IDEM_UNAVAILABLE);
    boot(&f);settled(&f);
    assert(ABoxMqttIdem_Find(&f.store.cache,"on1",digest,&index)==ABOX_IDEM_REPLAY && f.effects==1);
    f.disk.files[0].size=FILE_SIZE+1U; /* Valid CRC prefix plus trailing garbage. */
    boot(&f);settled(&f);assert(f.disk.files[0].size==FILE_SIZE && f.disk.files[1].size==FILE_SIZE);
    assert(ABoxMqttIdem_Find(&f.store.cache,"on1",digest,&index)==ABOX_IDEM_REPLAY);
    uint8_t changed[32]={8}; assert(ABoxMqttIdem_Find(&f.store.cache,"on1",changed,&other)==ABOX_IDEM_CONFLICT);
    begin(&f,"on2",&other);settled(&f);
    /* Reserved request survives reboot, but is never resumed. */
    boot(&f);settled(&f);
    assert(ABoxMqttIdem_Find(&f.store.cache,"on2",digest,&other)==ABOX_IDEM_UNCERTAIN);
    ABoxClockSnapshot clock=clock_now(&f);
    assert(ABoxMqttIdem_Begin(&f.store.cache,"on3",digest,clock.utc_ms+5000,&clock,&other)==4003);
    clock.quality=ABOX_CLOCK_UNTRUSTED;
    assert(ABoxMqttIdem_Find(&f.store.cache,"on1",digest,&index)==ABOX_IDEM_REPLAY);
    assert(!strcmp(ABoxMqttIdem_Response(&f.store.cache,index,NULL),response));
    clock=clock_now(&f);clock.utc_ms+=700000;clock.mono_ms+=700000;
    assert(ABoxMqttIdem_Begin(&f.store.cache,"on3",digest,clock.utc_ms+5000,&clock,&other)==200);
    settled(&f);
    clock.utc_ms=f.entries[other].expires_at;
    assert(ABoxMqttIdem_TakeExecutable(&f.store.cache,other,&clock)==4005);
    puts("PASS persistent first response, conflict, capacity/protection and interrupted reservation");
}
static void recover_and_check(Fixture *f)
{
    size_t index;f->disk.cut_write=0;boot(f);settled(f);
    ABoxMqttIdemMatch match=ABoxMqttIdem_Find(&f->store.cache,"on1",digest,&index);
    if (match==ABOX_IDEM_NEW) { begin(f,"on1",&index);settled(f);act(f,index);settled(f); }
    else assert(match==ABOX_IDEM_UNCERTAIN || match==ABOX_IDEM_REPLAY);
    assert(f->effects<=1);
}
static void cuts(void)
{
    unsigned cases=0;
    for (unsigned boundary=0;boundary<64;++boundary) {
        Fixture f={0};size_t index;boot(&f);settled(&f);begin(&f,"on1",&index);
        for (unsigned i=0;i<boundary;++i) {
            tick(&f);
            if (f.entries[index].state==ABOX_IDEM_PREPARED) act(&f,index);
        }
        recover_and_check(&f);++cases;
    }
    /* Every prefix of each of the two PREPARED and two FINAL snapshots. */
    for (unsigned write=1;write<=4;++write) for (size_t prefix=0;prefix<FILE_SIZE;++prefix) {
        Fixture f={0};size_t index;boot(&f);settled(&f);
        f.disk.writes=0;f.disk.cut_write=write;f.disk.prefix=prefix;begin(&f,"on1",&index);
        for (unsigned i=0;i<150;++i) {
            tick(&f);
            if (f.entries[index].state==ABOX_IDEM_PREPARED) act(&f,index);
            if (ABoxUfsJournal_Status(&f.store.journal)<0 || f.entries[index].state==ABOX_IDEM_FINAL) break;
        }
        assert(ABoxUfsJournal_Status(&f.store.journal)<0);
        recover_and_check(&f);++cases;
    }
    printf("PASS %u reset/write-prefix cuts: no second physical dispatch\n",cases);
}
static void failures(void)
{
    Fixture f={0};size_t index;
    assert(!ABoxMqttIdemUfs_Init(&f.store,NULL,NULL,0,NULL,0,NULL,NULL,NULL,NULL,NULL,0));
    assert(!ABoxMqttIdemUfs_Load(&f.store,0));ABoxMqttIdemUfs_Poll(&f.store,1);
    assert(ABoxMqttIdem_Find(&f.store.cache,"x",digest,&index)==ABOX_IDEM_UNAVAILABLE);
    boot(&f);settled(&f);
    f.disk.files[0].size=f.disk.files[1].size=FILE_SIZE+1U;boot(&f);settled(&f);
    assert(ABoxUfsJournal_Status(&f.store.journal)<0 && !f.disk.leased);
    assert(ABoxMqttIdem_Find(&f.store.cache,"x",digest,&index)==ABOX_IDEM_UNAVAILABLE);
    memset(&f,0,sizeof(f));boot(&f);settled(&f);
    f.disk.files[0].bytes[0]^=1;f.disk.files[1].bytes[0]^=1;boot(&f);settled(&f);
    assert(ABoxUfsJournal_Status(&f.store.journal)<0 && !f.disk.leased);
    memset(&f,0,sizeof(f));f.disk.initialized=1;boot(&f);settled(&f);
    assert(ABoxUfsJournal_Status(&f.store.journal)<0); /* Used, now missing: never recreate. */
    memset(&f,0,sizeof(f));boot(&f);settled(&f);
    f.disk.busy=1;begin(&f,"on1",&index);
    for (unsigned i=0;i<10;++i) tick(&f);
    ABoxClockSnapshot clock=clock_now(&f);assert(ABoxMqttIdem_TakeExecutable(&f.store.cache,index,&clock)==4003);
    f.disk.busy=0;settled(&f);f.disk.io_error=1;act(&f,index);settled(&f);
    assert(f.store.cache.failed && !ABoxMqttIdem_Response(&f.store.cache,index,NULL) && !f.disk.leased);
    ABoxMqttIdemUfs_OnModemReset(&f.store);
    assert(ABoxUfsJournal_Status(&f.store.journal)<0 && !ABoxMqttIdemUfs_Load(&f.store,f.mono));
    puts("PASS corrupt/missing media, resource contention and uncertain finalization");
}
int main(void) { basic();cuts();failures();return 0; }
