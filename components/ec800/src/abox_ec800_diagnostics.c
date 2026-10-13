#include "abox_ec800_diagnostics.h"
#include <stdio.h>
#include <string.h>

void ABoxEc800Diagnostics_OnModemReset(ABoxEc800Diagnostics *d)
{
    if (!d) return;
    d->pending = d->step = d->observed = d->sim = d->registration_valid = 0U;
    d->step = 2U; /* Enable numeric CME once after each physical modem reset. */
    d->due_ms = 0U;
    memset(&d->snapshot, 0, sizeof(d->snapshot));
}
static void line(ABoxEc800Event event, const uint8_t *data, uint16_t length, void *user)
{
    ABoxEc800Diagnostics *d = user;
    const char *s = (const char *)data;
    unsigned code, mode, status;
    if (event != ABOX_EC800_EVENT_LINE) return;
    if (length == 3U && !memcmp(data, "RDY", 3U)) {
        ABoxEc800Diagnostics_OnModemReset(d);
        return;
    }
    if (!d->pending || !ABoxEc800At_IsActive(d->at, d->owner)) return;
    if (!d->step && !strncmp(s, "+CPIN:", 6U)) {
        s += 6; while (*s == ' ') ++s;
        if (!strcmp(s, "READY")) d->sim = ABOX_EC800_DIAG_SIM_READY;
        else if (!strcmp(s, "SIM PIN") || !strcmp(s, "SIM PUK"))
            d->sim = ABOX_EC800_DIAG_SIM_PIN_REQUIRED;
        else return; /* NOT READY and unknown strings do not establish absence. */
        d->observed = 1U;
    } else if (!d->step && !strncmp(s, "+CME ERROR:", 11U)) {
        s += 11; while (*s == ' ') ++s;
        code = 0U;
        if (sscanf(s, "%u", &code) != 1) {
            if (!strcmp(s, "SIM not inserted")) code = 10U;
            else if (!strcmp(s, "SIM PIN required")) code = 11U;
            else if (!strcmp(s, "SIM PUK required")) code = 12U;
            else if (!strcmp(s, "SIM failure")) code = 13U;
        }
        d->snapshot.last_cme = (uint16_t)code;
        if (code == 10U) d->sim = ABOX_EC800_DIAG_SIM_NOT_INSERTED;
        else if (code == 11U || code == 12U) d->sim = ABOX_EC800_DIAG_SIM_PIN_REQUIRED;
        else if (code == 13U || code == 15U) d->sim = ABOX_EC800_DIAG_SIM_ERROR;
        else return;
        d->observed = 1U;
    } else if (d->step == 1U && sscanf(s, "+CEREG: %u,%u", &mode, &status) == 2 &&
               mode <= 5U && status <= 10U) {
        d->snapshot.registration = (uint8_t)status;
        d->observed = 1U;
    }
}
static void done(ABoxEc800Result result, void *user)
{
    ABoxEc800Diagnostics *d = user;
    uint32_t now = d->at->port.tick_ms(d->at->port.context);
    d->pending = 0U;
    d->snapshot.last_result = (uint8_t)result;
    d->snapshot.updated_at_ms = d->checked_at_ms = now;
    if (d->step == 2U && result != ABOX_EC800_RESULT_TIMEOUT) {
        d->step = 0U;
        d->due_ms = now;
        return;
    }
    if (result == ABOX_EC800_RESULT_TIMEOUT) {
        d->snapshot.state = ABOX_EC800_DIAG_MODEM_NO_RESPONSE;
        d->snapshot.valid = 1U;
        d->sim = d->registration_valid = 0U;
    } else if (!d->step) {
        d->snapshot.state = d->observed ? (ABoxEc800DiagState)d->sim : ABOX_EC800_DIAG_UNKNOWN;
        d->snapshot.valid = d->observed;
        if (result == ABOX_EC800_RESULT_OK && d->sim == ABOX_EC800_DIAG_SIM_READY && d->observed) {
            d->step = 1U;
            d->due_ms = now;
            return;
        }
    } else {
        d->registration_valid = result == ABOX_EC800_RESULT_OK && d->observed;
        d->snapshot.valid = 1U; /* CPIN READY remains established. */
        d->snapshot.state = d->registration_valid ? ABOX_EC800_DIAG_NETWORK_NOT_REGISTERED :
                                                   ABOX_EC800_DIAG_SIM_READY;
    }
    d->step = 0U;
    d->due_ms = now + ABOX_EC800_DIAG_RETRY_MS;
}
int ABoxEc800Diagnostics_Init(ABoxEc800Diagnostics *d, ABoxEc800At *at, ABoxEc800Owner owner)
{
    if (!d || !at || !at->port.tick_ms || !owner) return 0;
    memset(d, 0, sizeof(*d)); d->at = at; d->owner = owner;
    d->step = 2U;
    if (!ABoxEc800At_Register(at, owner, line, d)) {
        d->at = NULL;
        return 0;
    }
    return 1;
}
void ABoxEc800Diagnostics_Poll(ABoxEc800Diagnostics *d, uint32_t now,
    uint8_t may_query, uint8_t attached, uint8_t pdp_active, uint8_t mqtt_ready)
{
    if (!d || !d->at) return;
    if (d->snapshot.valid && (uint32_t)(now - d->checked_at_ms) >= ABOX_EC800_DIAG_VALID_MS) {
        d->snapshot.valid = d->sim = d->registration_valid = 0U;
        d->snapshot.state = ABOX_EC800_DIAG_UNKNOWN;
    }
    if (d->sim == ABOX_EC800_DIAG_SIM_READY) {
        if (mqtt_ready) d->snapshot.state = ABOX_EC800_DIAG_MQTT_READY;
        else if (d->registration_valid) {
            if (d->snapshot.registration != 1U && d->snapshot.registration != 5U)
                d->snapshot.state = ABOX_EC800_DIAG_NETWORK_NOT_REGISTERED;
            else d->snapshot.state = attached && pdp_active ? ABOX_EC800_DIAG_SIM_READY :
                                                            ABOX_EC800_DIAG_DATA_NOT_READY;
        } else d->snapshot.state = ABOX_EC800_DIAG_SIM_READY;
    } else if (mqtt_ready && !d->snapshot.valid) {
        d->snapshot.state = ABOX_EC800_DIAG_MQTT_READY;
    }
    if (!may_query || d->pending || ABoxEc800At_IsBusy(d->at) ||
        (d->due_ms && (int32_t)(now - d->due_ms) < 0)) return;
    d->observed = 0U;
    if (!d->step) {
        d->sim = d->registration_valid = 0U;
        d->snapshot.last_cme = 0U;
    }
    const char *command = d->step == 2U ? "AT+CMEE=1" : d->step == 1U ? "AT+CEREG?" : "AT+CPIN?";
    if (ABoxEc800At_Submit(d->at, command, d->owner,
        ABOX_EC800_PRIORITY_NORMAL, ABOX_EC800_DIAG_QUERY_TIMEOUT_MS, done, d)) d->pending = 1U;
}
void ABoxEc800Diagnostics_Get(const ABoxEc800Diagnostics *d, ABoxEc800DiagSnapshot *out)
{ if (d && out) *out = d->snapshot; }
