#include "abox_ec800_iccid.h"
#include <string.h>

static void on_line(ABoxEc800Event event, const uint8_t *data,
                    uint16_t length, void *user)
{
    ABoxEc800Iccid *identity = (ABoxEc800Iccid *)user;
    uint16_t offset = 7U;
    if (!identity || !identity->pending || event != ABOX_EC800_EVENT_LINE ||
        length < offset || memcmp(data, "+QCCID:", offset) != 0) return;
    while (offset < length && (data[offset] == ' ' || data[offset] == '\t'))
        ++offset;
    if (offset == length || (size_t)(length - offset) >= sizeof(identity->value)) return;
    memcpy(identity->value, data + offset, length - offset);
    identity->value[length - offset] = '\0';
    identity->valid = 1U;
    identity->pending = 0U;
}

static void on_done(ABoxEc800Result result, void *user)
{
    ABoxEc800Iccid *identity = (ABoxEc800Iccid *)user;
    if (identity && result != ABOX_EC800_RESULT_OK) identity->pending = 0U;
}

int ABoxEc800Iccid_Init(ABoxEc800Iccid *identity, ABoxEc800At *at,
                         ABoxEc800Owner owner)
{
    if (!identity || !at || owner == ABOX_EC800_OWNER_NONE) return 0;
    memset(identity, 0, sizeof(*identity));
    identity->at = at;
    identity->owner = owner;
    if (!ABoxEc800At_Register(at, owner, on_line, identity)) {
        identity->at = NULL;
        return 0;
    }
    return 1;
}

int ABoxEc800Iccid_Request(ABoxEc800Iccid *identity, uint32_t now_ms)
{
    if (!identity || !identity->at || identity->pending ||
        !ABoxEc800At_Submit(identity->at, "AT+QCCID", identity->owner,
                            ABOX_EC800_PRIORITY_NORMAL, 5000U,
                            on_done, identity)) return 0;
    identity->pending = 1U;
    identity->started_ms = now_ms;
    return 1;
}

void ABoxEc800Iccid_Poll(ABoxEc800Iccid *identity, uint32_t now_ms)
{
    if (identity && identity->pending &&
        (uint32_t)(now_ms - identity->started_ms) > 5000U)
        identity->pending = 0U;
}

void ABoxEc800Iccid_OnModemReset(ABoxEc800Iccid *identity)
{
    if (!identity) return;
    identity->pending = identity->valid = 0U;
    identity->value[0] = '\0';
}

const char *ABoxEc800Iccid_Value(const ABoxEc800Iccid *identity)
{
    return identity && identity->valid ? identity->value : 0;
}
