#include "abox_ec800_ufs_at.h"
#include <stdio.h>
#include <string.h>

enum { CMEE, OPEN, TRANSFER, CLOSE, LIST, REMOVE };
static void release(ABoxEc800UfsAt *b)
{
    if (b->leased && b->lease.release) b->lease.release(b->lease.context);
    b->leased = 0;
}
static void finish(ABoxEc800UfsAt *b, ABoxAsyncStatus result)
{ b->status = result; release(b); }
static void step(ABoxEc800UfsAt *b, uint8_t state)
{
    b->state = state; b->submitted = b->done = 0;
    b->ok = 1; b->cme = 0;
}
static void completed(ABoxEc800Result result, void *user)
{
    ABoxEc800UfsAt *b = user;
    b->done = 1;
    if (result != ABOX_EC800_RESULT_OK) b->ok = 0;
    if (result == ABOX_EC800_RESULT_TIMEOUT) {
        ABoxEc800At_Quarantine(b->at);
        finish(b, ABOX_ASYNC_QUARANTINED);
    }
}
static void event(ABoxEc800Event kind, const uint8_t *data, uint16_t length, void *user)
{
    ABoxEc800UfsAt *b = user;
    unsigned a, n; const char *line = (const char *)data;
    if (kind == ABOX_EC800_EVENT_LINE && length == 3 && !memcmp(data, "RDY", 3)) {
        if (b->status == ABOX_ASYNC_PENDING) finish(b, ABOX_ASYNC_ERROR);
        return;
    }
    if (b->status != ABOX_ASYNC_PENDING || !b->submitted) return;
    if (kind == ABOX_EC800_EVENT_RAW) {
        if (b->state != TRANSFER || b->request.operation != ABOX_UFS_READ ||
            length > b->request.size - b->transferred) {
            ABoxEc800At_Quarantine(b->at); finish(b, ABOX_ASYNC_QUARANTINED); return;
        }
        memcpy(b->request.data + b->transferred, data, length);
        b->transferred += length; return;
    }
    if (sscanf(line, "+CME ERROR: %u", &a) == 1) b->cme = a <= UINT16_MAX ? (uint16_t)a : UINT16_MAX;
    else if (sscanf(line, "+QFOPEN: %u", &a) == 1) {
        if (a <= UINT16_MAX) { b->handle = (uint16_t)a; b->handle_seen = 1; }
        else b->ok = 0;
    } else if (!strncmp(line, "CONNECT", 7)) {
        if (b->state != TRANSFER) { b->ok = 0; return; }
        if (b->request.operation == ABOX_UFS_UPLOAD) {
            if (!ABoxEc800At_SendPayload(b->at, b->owner, b->request.data+b->chunk_start, (uint16_t)b->chunk_size)) b->ok = 0;
        } else if (sscanf(line, "CONNECT %u", &a) == 1 && a && a <= b->chunk_size &&
                   ABoxEc800At_BeginRaw(b->at, b->owner, a)) b->raw_seen = 1;
        else { ABoxEc800At_Quarantine(b->at); finish(b, ABOX_ASYNC_QUARANTINED); }
    } else if (sscanf(line, "+QFWRITE: %u,%u", &a, &n) == 2)
        b->write_seen = a == b->chunk_size && n == b->chunk_size;
    /* In LIST state an empty successful listing proves absence. An exact-name
     * request returning any file cannot be treated as first use. */
    else if (b->state == LIST && !strncmp(line, "+QFLST:", 7)) b->handle_seen = 1;
}
static int start(void *context, const ABoxUfsRequest *r, uint64_t *operation)
{
    ABoxEc800UfsAt *b = context;
    if (!b || !b->at || !r || !operation || b->status == ABOX_ASYNC_PENDING || b->at->quarantined ||
        !ABoxEc800Ufs_PathValid(r->path) || (unsigned)r->operation > ABOX_UFS_REMOVE ||
        (r->operation != ABOX_UFS_REMOVE && (!r->data || !r->size)) ||
        b->operation == UINT64_MAX) return 0;
    if (b->lease.acquire && !b->lease.acquire(b->lease.context)) return 0;
    b->leased = 1; b->request = *r; strcpy(b->path, r->path); b->request.path = b->path;
    b->transferred = 0; b->handle_seen = b->raw_seen = b->write_seen = 0;
    b->short_read = b->transfer_failed = 0;
    *operation = ++b->operation; b->status = ABOX_ASYNC_PENDING; step(b, CMEE); return 1;
}
static ABoxAsyncStatus poll(void *context, uint64_t operation, uint32_t *transferred)
{
    ABoxEc800UfsAt *b = context;
    if (!b || operation != b->operation) return ABOX_ASYNC_ERROR;
    ABoxEc800UfsAt_Poll(b);
    if (transferred) *transferred = b->transferred;
    return b->status;
}
static int cancel(void *context, uint64_t operation)
{
    ABoxEc800UfsAt *b = context;
    if (!b || operation != b->operation) return 0;
    if (b->status != ABOX_ASYNC_PENDING) return !b->at->quarantined;
    /* Untagged file replies and an open handle cannot be safely reassigned. */
    if (b->handle_seen || ABoxEc800At_IsActive(b->at, b->owner)) {
        ABoxEc800At_Quarantine(b->at); finish(b, ABOX_ASYNC_QUARANTINED); return 0;
    }
    ABoxEc800At_CancelQueued(b->at, b->owner);
    finish(b, ABOX_ASYNC_CANCELLED); return 1;
}
int ABoxEc800UfsAt_Init(ABoxEc800UfsAt *b, ABoxEc800At *at,
    ABoxEc800Owner owner, const ABoxUfsLeasePort *lease)
{
    if (!b) return 0;
    memset(b,0,sizeof(*b));
    if (!at || !at->port.tick_ms || !at->port.write || owner < ABOX_EC800_OWNER_PRODUCT_BASE ||
        (lease && (!lease->acquire || !lease->release))) return 0;
    b->at = at; b->owner = owner;
    if (lease) b->lease = *lease;
    if (!ABoxEc800At_Register(at, owner, event, b)) { b->at=NULL; return 0; }
    return 1;
}
ABoxEc800UfsPort ABoxEc800UfsAt_Port(ABoxEc800UfsAt *b)
{ ABoxEc800UfsPort p = {b, start, poll, cancel}; return p; }
void ABoxEc800UfsAt_Poll(ABoxEc800UfsAt *b)
{
    char command[128];
    if (!b || b->status != ABOX_ASYNC_PENDING) return;
    if (b->at->quarantined) { finish(b, ABOX_ASYNC_QUARANTINED); return; }
    if (b->submitted) {
        if (!b->done) return;
        if (b->state == OPEN && !b->ok && b->cme == 410U) {
            b->handle_seen = 0; step(b, LIST); return;
        }
        if ((b->state == OPEN || b->state == LIST) && !b->ok && b->cme == 405U) {
            finish(b, ABOX_ASYNC_NOT_FOUND); return;
        }
        if (b->state == LIST && b->ok && !b->handle_seen) { finish(b, ABOX_ASYNC_NOT_FOUND); return; }
        if (!b->ok || b->state == LIST) {
            if (b->handle_seen) {
                /* Close on ordinary short-read errors before declaring failure. */
                if (b->state == TRANSFER) {
                    if (b->request.operation == ABOX_UFS_READ && b->cme == 402U) b->short_read=1;
                    else b->transfer_failed=1;
                    b->raw_seen = b->write_seen = 0; step(b, CLOSE); return;
                }
                ABoxEc800At_Quarantine(b->at);
            }
            finish(b, b->at->quarantined ? ABOX_ASYNC_QUARANTINED : ABOX_ASYNC_ERROR); return;
        }
        switch (b->state) {
        case CMEE: step(b, b->request.operation == ABOX_UFS_REMOVE ? REMOVE : OPEN); break;
        case OPEN:
            if (!b->handle_seen) { finish(b, ABOX_ASYNC_ERROR); return; }
            step(b, TRANSFER); break;
        case TRANSFER:
            if (b->request.operation==ABOX_UFS_UPLOAD && b->write_seen) b->transferred+=b->chunk_size;
            if ((b->request.operation==ABOX_UFS_READ ? b->raw_seen : b->write_seen) &&
                b->transferred-b->chunk_start==b->chunk_size && b->transferred<b->request.size) {
                b->raw_seen=b->write_seen=0; step(b,TRANSFER);
            } else {
                if (b->request.operation==ABOX_UFS_READ && b->transferred-b->chunk_start<b->chunk_size) b->short_read=1;
                step(b,CLOSE);
            }
            break;
        case CLOSE:
            b->handle_seen = 0;
            if (b->transfer_failed) { finish(b,ABOX_ASYNC_ERROR); return; }
            if (b->short_read) { finish(b,ABOX_ASYNC_TRUNCATED); return; }
            finish(b, (b->request.operation == ABOX_UFS_READ ? b->raw_seen : b->write_seen) &&
                b->transferred == b->request.size ? ABOX_ASYNC_OK : ABOX_ASYNC_ERROR); return;
        case REMOVE: finish(b, ABOX_ASYNC_OK); return;
        default: finish(b, ABOX_ASYNC_ERROR); return;
        }
    }
    switch (b->state) {
    case CMEE: strcpy(command, "AT+CMEE=1"); break;
    case OPEN: snprintf(command, sizeof(command), "AT+QFOPEN=\"%s\",%u", b->path,
        b->request.operation == ABOX_UFS_UPLOAD ? 1U : 2U); break;
    case LIST: snprintf(command, sizeof(command), "AT+QFLST=\"%s\"", b->path); break;
    case TRANSFER:
        b->chunk_start=b->transferred;
        b->chunk_size=b->request.size-b->transferred;
        if (b->chunk_size>ABOX_EC800_AT_RAW_MAX) b->chunk_size=ABOX_EC800_AT_RAW_MAX;
        snprintf(command, sizeof(command), b->request.operation == ABOX_UFS_UPLOAD ?
            "AT+QFWRITE=%u,%lu,10" : "AT+QFREAD=%u,%lu", b->handle, (unsigned long)b->chunk_size); break;
    case CLOSE: snprintf(command, sizeof(command), "AT+QFCLOSE=%u", b->handle); break;
    case REMOVE: snprintf(command, sizeof(command), "AT+QFDEL=\"%s\"", b->path); break;
    default: return;
    }
    /* Set flags first: UART write failure may complete synchronously. */
    b->submitted = 1; b->done = 0;
    if (!ABoxEc800At_Submit(b->at, command, b->owner, ABOX_EC800_PRIORITY_NORMAL, 3000U, completed, b)) b->submitted = 0;
}
