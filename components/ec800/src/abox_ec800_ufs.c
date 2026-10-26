#include "abox_ec800_ufs.h"
#include <string.h>
int ABoxEc800Ufs_PathValid(const char *path)
{
    unsigned i;
    if (!path || strncmp(path, "UFS:", 4) || !path[4]) return 0;
    for (i = 4; i < ABOX_EC800_UFS_PATH_SIZE; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (!c) return 1;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 0;
}
int ABoxEc800Ufs_Init(ABoxEc800Ufs *u, const ABoxEc800UfsPort *p)
{
    if (!u || !p || !p->start || !p->poll || !p->cancel) return 0;
    memset(u, 0, sizeof(*u)); u->port = *p; return 1;
}
int ABoxEc800Ufs_Start(ABoxEc800Ufs *u, const ABoxUfsRequest *r,
                      uint32_t now, uint32_t timeout)
{
    if (!u || !u->port.start || !r || !timeout || timeout > INT32_MAX ||
        u->status == ABOX_ASYNC_PENDING || u->status == ABOX_ASYNC_QUARANTINED ||
        !ABoxEc800Ufs_PathValid(r->path) ||
        (unsigned)r->operation > (unsigned)ABOX_UFS_REMOVE ||
        (r->operation != ABOX_UFS_REMOVE && (!r->data || !r->size))) return 0;
    u->request = *r;
    strcpy(u->path, r->path); u->request.path = u->path;
    if (!u->port.start(u->port.context, &u->request, &u->operation)) return 0;
    u->started = now; u->timeout = timeout; u->transferred = 0;
    u->status = ABOX_ASYNC_PENDING; return 1;
}
static void stop(ABoxEc800Ufs *u, ABoxAsyncStatus reason)
{
    u->status = u->port.cancel(u->port.context, u->operation) ? reason : ABOX_ASYNC_QUARANTINED;
}
void ABoxEc800Ufs_Poll(ABoxEc800Ufs *u, uint32_t now)
{
    ABoxAsyncStatus status;
    if (!u || u->status != ABOX_ASYNC_PENDING) return;
    if ((uint32_t)(now - u->started) >= u->timeout) { stop(u, ABOX_ASYNC_TIMEOUT); return; }
    status = u->port.poll(u->port.context, u->operation, &u->transferred);
    if (status == ABOX_ASYNC_PENDING) return;
    if (status == ABOX_ASYNC_NOT_FOUND || status == ABOX_ASYNC_TRUNCATED) { u->status = status; return; }
    if (status != ABOX_ASYNC_OK) { stop(u, ABOX_ASYNC_ERROR); return; }
    u->status = (u->request.operation == ABOX_UFS_REMOVE ||
                 u->transferred == u->request.size) ? ABOX_ASYNC_OK : ABOX_ASYNC_ERROR;
}
void ABoxEc800Ufs_Cancel(ABoxEc800Ufs *u)
{ if (u && u->status == ABOX_ASYNC_PENDING) stop(u, ABOX_ASYNC_CANCELLED); }
void ABoxEc800Ufs_OnModemReset(ABoxEc800Ufs *u)
{ if (u) { u->status = ABOX_ASYNC_IDLE; u->operation = 0; } }
