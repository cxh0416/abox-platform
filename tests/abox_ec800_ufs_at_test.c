#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "abox_ec800_ufs_at.h"
typedef struct {
    ABoxEc800At at;ABoxEc800UfsAt backend;ABoxEc800Ufs ufs;
    uint32_t now;char command[256];uint8_t file[2600],sent[1024];
    unsigned size,position,sent_size,closes,peers;int exists,fail_close,short_write;
} Fixture;
static uint32_t tick(void *context) { return ((Fixture *)context)->now; }
static int write(void *context,const uint8_t *bytes,uint16_t length,uint32_t timeout)
{
    Fixture *f=context;(void)timeout;
    if (length>=2 && bytes[0]=='A' && bytes[1]=='T') {
        assert(length<sizeof(f->command));memcpy(f->command,bytes,length);f->command[length]=0;
    } else { assert(length<=sizeof(f->sent));memcpy(f->sent,bytes,length);f->sent_size=length; }
    return 1;
}
static void feed(Fixture *f,const char *line)
{ ABoxEc800At_Feed(&f->at,(const uint8_t *)line,(uint16_t)strlen(line)); }
static void answer(Fixture *f)
{
    char command[256],line[80];unsigned mode,size,handle;char path[80];
    if (!f->command[0]) return;
    strcpy(command,f->command);f->command[0]=0;
    if (!strncmp(command,"AT+CMEE=1",9)) feed(f,"\r\nOK\r\n");
    else if (!strncmp(command,"AT+PING",7)) { ++f->peers;feed(f,"\r\nOK\r\n"); }
    else if (sscanf(command,"AT+QFOPEN=\"%79[^\"]\",%u",path,&mode)==2) {
        if (mode==2 && !f->exists) { feed(f,"\r\n+CME ERROR: 410\r\n");return; }
        if (mode==1) { f->exists=1;f->size=0; }
        f->position=0;feed(f,"\r\n+QFOPEN: 7\r\nOK\r\n");
    } else if (!strncmp(command,"AT+QFLST=",9)) feed(f,"\r\nOK\r\n");
    else if (sscanf(command,"AT+QFWRITE=%u,%u",&handle,&size)==2) {
        assert(handle==7 && size<=1024);f->sent_size=0;feed(f,"\r\nCONNECT\r\n");
        assert(f->sent_size==size && f->position+size<=sizeof(f->file));
        memcpy(f->file+f->position,f->sent,size);f->position+=size;f->size=f->position;
        snprintf(line,sizeof(line),"\r\n+QFWRITE: %u,%u\r\nOK\r\n",f->short_write ? size-1U : size,size);feed(f,line);
    } else if (sscanf(command,"AT+QFREAD=%u,%u",&handle,&size)==2) {
        assert(handle==7 && size<=1024);
        if (f->position==f->size) { feed(f,"\r\n+CME ERROR: 402\r\n");return; }
        if (size>f->size-f->position) size=f->size-f->position;
        snprintf(line,sizeof(line),"\r\nCONNECT %u\r\n",size);feed(f,line);
        ABoxEc800At_Feed(&f->at,f->file+f->position,(uint16_t)size);f->position+=size;feed(f,"\r\nOK\r\n");
    } else if (!strncmp(command,"AT+QFCLOSE=",11)) {
        ++f->closes;feed(f,f->fail_close ? "\r\nERROR\r\n" : "\r\nOK\r\n");
    } else assert(0);
}
static void run(Fixture *f)
{
    for (unsigned i=0;i<300 && f->ufs.status==ABOX_ASYNC_PENDING;++i) {
        ++f->now;ABoxEc800Ufs_Poll(&f->ufs,f->now);ABoxEc800At_Task(&f->at);answer(f);
    }
    assert(f->ufs.status!=ABOX_ASYNC_PENDING);
}
int main(void)
{
    Fixture f={0};ABoxEc800AtPort at_port={&f,tick,write,NULL,NULL};
    assert(ABoxEc800At_Init(&f.at,&at_port));
    assert(ABoxEc800UfsAt_Init(&f.backend,&f.at,ABOX_EC800_OWNER_PRODUCT_BASE,NULL));
    ABoxEc800UfsPort port=ABoxEc800UfsAt_Port(&f.backend);assert(ABoxEc800Ufs_Init(&f.ufs,&port));
    uint8_t data[2500],readback[2500];for (unsigned i=0;i<sizeof(data);++i) data[i]=(uint8_t)(i%251);
    ABoxUfsRequest r={ABOX_UFS_READ,"UFS:test.bin",readback,sizeof(readback)};
    assert(ABoxEc800Ufs_Start(&f.ufs,&r,f.now,15000));run(&f);assert(f.ufs.status==ABOX_ASYNC_NOT_FOUND);
    r.operation=ABOX_UFS_UPLOAD;r.data=data;
    assert(ABoxEc800Ufs_Start(&f.ufs,&r,f.now,15000));run(&f);
    assert(f.ufs.status==ABOX_ASYNC_OK && f.size==sizeof(data) && !memcmp(data,f.file,sizeof(data)));
    r.operation=ABOX_UFS_READ;r.data=readback;
    assert(ABoxEc800Ufs_Start(&f.ufs,&r,f.now,15000));run(&f);
    assert(f.ufs.status==ABOX_ASYNC_OK && !memcmp(data,readback,sizeof(data)));
    f.size=100;
    assert(ABoxEc800Ufs_Start(&f.ufs,&r,f.now,15000));run(&f);
    assert(f.ufs.status==ABOX_ASYNC_TRUNCATED && !f.at.quarantined);
    assert(ABoxEc800At_Submit(&f.at,"AT+PING",ABOX_EC800_OWNER_PRODUCT_BASE+1U,
        ABOX_EC800_PRIORITY_NORMAL,1000,NULL,NULL));
    f.size=0;
    assert(ABoxEc800Ufs_Start(&f.ufs,&r,f.now,15000));run(&f);
    assert(f.ufs.status==ABOX_ASYNC_TRUNCATED && !f.at.quarantined && f.peers==1);
    r.operation=ABOX_UFS_UPLOAD;r.data=data;f.short_write=1;
    assert(ABoxEc800Ufs_Start(&f.ufs,&r,f.now,15000));run(&f);
    assert(f.ufs.status==ABOX_ASYNC_ERROR && !f.at.quarantined);
    r.operation=ABOX_UFS_READ;r.data=readback;f.short_write=0;
    f.size=sizeof(data);f.fail_close=1;
    assert(ABoxEc800Ufs_Start(&f.ufs,&r,f.now,15000));run(&f);
    assert(f.ufs.status!=ABOX_ASYNC_OK && f.at.quarantined);
    puts("PASS real AT backend: chunked read/write, 410/list absence, short/empty file and unsafe close");
    return 0;
}
