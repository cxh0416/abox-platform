#include "abox_ec800_command_port.h"
#include <string.h>
static void done(ABoxEc800Result result, void *user)
{
    ABoxEc800CommandAdapter *a = user;
    if (result == ABOX_EC800_RESULT_TIMEOUT || result == ABOX_EC800_RESULT_SEND_FAIL) {
        ABoxEc800At_Quarantine(a->at);
        a->result = ABOX_ASYNC_QUARANTINED;
    } else if (a->at->quarantined) a->result = ABOX_ASYNC_QUARANTINED;
    else a->result = result == ABOX_EC800_RESULT_OK ? ABOX_ASYNC_OK : ABOX_ASYNC_ERROR;
}
static int submit(void *user, const char *command, uint32_t timeout, uint64_t *operation)
{
    ABoxEc800CommandAdapter *a = user;
    if (!operation || !command || strlen(command) > ABOX_EC800_AT_COMMAND_SIZE - 3U ||
        !timeout || a->result == ABOX_ASYNC_PENDING || a->at->quarantined ||
        a->serial == UINT64_MAX || ABoxEc800At_HasPending(a->at, a->owner)) return 0;
    if (!ABoxEc800At_Submit(a->at, command, a->owner, ABOX_EC800_PRIORITY_HIGH,
                            timeout, done, a)) return 0;
    *operation = a->operation = ++a->serial;
    a->result = ABOX_ASYNC_PENDING;
    return 1;
}
static ABoxAsyncStatus poll(void *user, uint64_t operation)
{
    ABoxEc800CommandAdapter *a = user;
    if (!operation || operation != a->operation) return ABOX_ASYNC_ERROR;
    return a->result;
}
static int cancel(void *user, uint64_t operation)
{
    ABoxEc800CommandAdapter *a = user;
    if (!operation || operation != a->operation || a->at->quarantined) return 0;
    if (a->result != ABOX_ASYNC_PENDING) return 1;
    if (ABoxEc800At_IsActive(a->at, a->owner)) {
        ABoxEc800At_Quarantine(a->at);
        a->result = ABOX_ASYNC_QUARANTINED;
        return 0;
    }
    ABoxEc800At_CancelQueued(a->at, a->owner);
    a->result = ABOX_ASYNC_CANCELLED;
    return 1;
}
int ABoxEc800CommandAdapter_Init(ABoxEc800CommandAdapter *a, ABoxEc800At *at,
                                 ABoxEc800Owner owner, ABoxEc800CommandPort *p)
{
    if (!a || !at || !owner || !p) return 0;
    memset(a, 0, sizeof(*a)); a->at = at; a->owner = owner;
    p->context = a; p->submit = submit; p->poll = poll; p->cancel = cancel;
    return 1;
}
void ABoxEc800CommandAdapter_OnModemReset(ABoxEc800CommandAdapter *a)
{ if (a) { a->operation = 0; a->result = ABOX_ASYNC_IDLE; } }
