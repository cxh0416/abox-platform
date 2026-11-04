#include "abox_ec800_tls.h"
#include <stdio.h>
#include <string.h>
static ABoxTlsSlot *slot(ABoxEc800Tls *t, ABoxTlsLease l)
{
    if (!t || l.context >= ABOX_TLS_CONTEXT_CAPACITY || !l.generation ||
        t->slots[l.context].generation != l.generation || !t->slots[l.context].owner) return NULL;
    return &t->slots[l.context];
}
int ABoxEc800Tls_Init(ABoxEc800Tls *t, const ABoxEc800CommandPort *p, uint8_t mask)
{
    if (!t || !p || !p->submit || !p->poll || !p->cancel) return 0;
    memset(t, 0, sizeof(*t)); t->port = *p;
    t->supported_mask = (uint8_t)(mask & ~(1U << ABOX_TLS_LEGACY_HTTPS_CONTEXT)); return 1;
}
int ABoxEc800Tls_SetSupportedMask(ABoxEc800Tls *t, uint8_t mask)
{
    uint8_t id;
    if (!t) return 0;
    for (id = 0U; id < ABOX_TLS_CONTEXT_CAPACITY; ++id)
        if (t->slots[id].owner) return 0;
    t->supported_mask = (uint8_t)(mask & ~(1U << ABOX_TLS_LEGACY_HTTPS_CONTEXT));
    return 1;
}
int ABoxEc800Tls_Acquire(ABoxEc800Tls *t, uint8_t id, uint32_t owner, ABoxTlsLease *l)
{
    ABoxTlsSlot *s;
    if (!t || !l || !owner || id >= ABOX_TLS_CONTEXT_CAPACITY ||
        !(t->supported_mask & (1U << id)) || t->slots[id].owner || t->serial == UINT64_MAX) return 0;
    s = &t->slots[id]; memset(s, 0, sizeof(*s));
    s->owner = owner; s->generation = ++t->serial;
    l->context = id; l->generation = s->generation; return 1;
}
int ABoxEc800Tls_Release(ABoxEc800Tls *t, ABoxTlsLease l)
{
    ABoxTlsSlot *s = slot(t, l);
    if (!s || s->pins || s->status == ABOX_ASYNC_PENDING ||
        s->status == ABOX_ASYNC_QUARANTINED) return 0;
    memset(s, 0, sizeof(*s)); return 1;
}
int ABoxEc800Tls_Prepare(ABoxEc800Tls *t, ABoxTlsLease l,
                         const ABoxTlsProfile *p, uint32_t now, uint32_t timeout)
{
    ABoxTlsSlot *s = slot(t, l);
    if (!s || !p || p->psk_a10 > 1U || p->psk_sni > 2U || !ABoxTlsCredentials_Valid(&p->credentials) ||
        (p->credentials.mode == ABOX_TLS_AUTH_CA &&
         (!p->ca_verified || !p->time_valid || !p->ca_revision ||
          !ABoxEc800Ufs_PathValid(p->ca_file) ||
          (p->ca_ciphersuite && p->ca_ciphersuite != 0x009C && p->ca_ciphersuite != 0xC02B))) || !timeout || timeout > INT32_MAX ||
        s->status != ABOX_ASYNC_IDLE || s->pins) return 0;
    s->auth_mode = p->credentials.mode;
    s->ca_ciphersuite = p->ca_ciphersuite ? p->ca_ciphersuite : 0x009C;
    s->psk_a10 = p->psk_a10;
    s->psk_sni = p->psk_sni;
    if (s->auth_mode == ABOX_TLS_AUTH_PSK) {
        memcpy(s->psk.identity, p->credentials.identity, sizeof(s->psk.identity));
        memcpy(s->psk.secret, p->credentials.secret, sizeof(s->psk.secret));
    }
    if (p->credentials.mode == ABOX_TLS_AUTH_CA) {
        strcpy(s->ca_file, p->ca_file); s->revision = p->ca_revision;
    }
    s->started = now; s->timeout = timeout; s->status = ABOX_ASYNC_PENDING; return 1;
}
static void stop(ABoxEc800Tls *t, ABoxTlsSlot *s, ABoxAsyncStatus reason)
{
    if (s->waiting && !t->port.cancel(t->port.context, s->operation))
        s->status = ABOX_ASYNC_QUARANTINED;
    else s->status = reason;
    s->waiting = 0;
}
void ABoxEc800Tls_Poll(ABoxEc800Tls *t, uint32_t now)
{
    uint8_t id;
    if (!t) return;
    for (id = 0; id < ABOX_TLS_CONTEXT_CAPACITY; ++id) {
        ABoxTlsSlot *s = &t->slots[id];
        char command[160];
        if (s->status != ABOX_ASYNC_PENDING) continue;
        if ((uint32_t)(now - s->started) >= s->timeout) { stop(t, s, ABOX_ASYNC_TIMEOUT); continue; }
        if (s->waiting) {
            ABoxAsyncStatus result = t->port.poll(t->port.context, s->operation);
            if (result == ABOX_ASYNC_PENDING) continue;
            if (result != ABOX_ASYNC_OK) { stop(t, s, ABOX_ASYNC_ERROR); continue; }
            s->waiting = 0;
            if (++s->step == (s->auth_mode == ABOX_TLS_AUTH_PSK ? (s->psk_sni ? 6 : 5) : 9)) {
                if (s->auth_mode == ABOX_TLS_AUTH_PSK)
                    memset(s->psk.secret, 0, sizeof(s->psk.secret));
                s->status = ABOX_ASYNC_OK; continue;
            }
        }
        if (s->auth_mode == ABOX_TLS_AUTH_PSK) {
            switch (s->step) {
            case 0: snprintf(command, sizeof(command), "AT+QSSLCFG=\"sslversion\",%u,3", id); break;
            case 1: snprintf(command, sizeof(command), "AT+QSSLCFG=\"ciphersuite\",%u,0x%04X", id,
                             s->psk_a10 ? 0x00AE : 0xCCAC); break;
            case 2: snprintf(command, sizeof(command), "AT+QSSLCFG=\"seclevel\",%u,0", id); break;
            case 3: snprintf(command, sizeof(command), "AT+QSSLCFG=\"session_cache\",%u,0", id); break;
            default:
            if (s->psk_sni && s->step == 4U) {
                snprintf(command, sizeof(command), "AT+QSSLCFG=\"sni\",%u,%u", id,
                         s->psk_sni == 1U ? 1U : 0U);
            } else if (s->psk_a10) {
                static const char hex[] = "0123456789ABCDEF";
                char encoded[65];
                unsigned i;
                for (i = 0; i < 32U; ++i) {
                    uint8_t byte = (uint8_t)s->psk.secret[i];
                    encoded[2U * i] = hex[byte >> 4];
                    encoded[2U * i + 1U] = hex[byte & 15U];
                }
                encoded[64] = '\0';
                snprintf(command, sizeof(command), "AT+QSSLCFG=\"psk\",%u,\"%s\",\"%s\"", id,
                         encoded, s->psk.identity);
                memset(encoded, 0, sizeof(encoded));
            } else {
                snprintf(command, sizeof(command), "AT+QSSLCFG=\"psk\",%u,\"%s\",\"%.32s\"", id,
                         s->psk.identity, s->psk.secret); break;
            }
            break;
            }
        } else {
        switch (s->step) {
        case 0: snprintf(command, sizeof(command), "AT+QSSLCFG=\"sslversion\",%u,3", id); break;
        case 1: snprintf(command, sizeof(command), "AT+QSSLCFG=\"seclevel\",%u,1", id); break;
        case 2: snprintf(command, sizeof(command), "AT+QSSLCFG=\"sni\",%u,1", id); break;
        case 3: snprintf(command, sizeof(command), "AT+QSSLCFG=\"ignorelocaltime\",%u,0", id); break;
        /* A modem's default or previous context must not weaken verification. */
        case 4: snprintf(command, sizeof(command), "AT+QSSLCFG=\"ignorecertitem\",%u,0", id); break;
        case 5: snprintf(command, sizeof(command), "AT+QSSLCFG=\"ignoreinvalidcertsign\",%u,0", id); break;
        case 6: snprintf(command, sizeof(command), "AT+QSSLCFG=\"ignoremulticertchainverify\",%u,0", id); break;
        case 7: snprintf(command, sizeof(command), "AT+QSSLCFG=\"cacert\",%u,\"%s\"", id, s->ca_file); break;
        /* EC800 rejects an empty PSK. Select a certificate-only suite instead
         * of enabling all suites with a retained PSK in a reused context.
         * RSA remains the default; product MQTT may select an ECDSA suite. */
        default: snprintf(command, sizeof(command), "AT+QSSLCFG=\"ciphersuite\",%u,0x%04X", id, s->ca_ciphersuite); break;
        }
        }
        /* Rejected enqueue is retried until the overall deadline. */
        s->waiting = (uint8_t)!!t->port.submit(t->port.context, command,
                     s->timeout - (uint32_t)(now - s->started), &s->operation);
        memset(command, 0, sizeof(command));
    }
}
ABoxAsyncStatus ABoxEc800Tls_Status(const ABoxEc800Tls *t, ABoxTlsLease l)
{
    if (!t || l.context >= ABOX_TLS_CONTEXT_CAPACITY || !l.generation ||
        !t->slots[l.context].owner || t->slots[l.context].generation != l.generation) return ABOX_ASYNC_ERROR;
    return t->slots[l.context].status;
}
int ABoxEc800Tls_Pin(ABoxEc800Tls *t, ABoxTlsLease l)
{
    ABoxTlsSlot *s = slot(t, l);
    if (!s || s->status != ABOX_ASYNC_OK || s->pins == UINT16_MAX) return 0;
    ++s->pins; return 1;
}
int ABoxEc800Tls_Unpin(ABoxEc800Tls *t, ABoxTlsLease l)
{
    ABoxTlsSlot *s = slot(t, l);
    if (!s || !s->pins) return 0;
    --s->pins; return 1;
}
void ABoxEc800Tls_Cancel(ABoxEc800Tls *t, ABoxTlsLease l)
{ ABoxTlsSlot *s = slot(t, l); if (s && s->status == ABOX_ASYNC_PENDING) stop(t, s, ABOX_ASYNC_CANCELLED); }
void ABoxEc800Tls_OnModemReset(ABoxEc800Tls *t)
{ if (t) memset(t->slots, 0, sizeof(t->slots)); }
