#include "abox_time_sync.h"
#include "abox_clock.h"
#include <stdio.h>
#include <string.h>
void ABoxTimeSync_Init(ABoxTimeSync *s) { if (s) memset(s,0,sizeof(*s)); }
void ABoxTimeSync_Poll(ABoxTimeSync *s, const ABoxTimeSyncPort *p,
                        uint8_t ready, uint64_t now, uint64_t seed)
{
    if (!s || !p || !p->send || !p->apply) return;
    if (!ready) { s->ready = s->pending = s->attempts = s->have_best = 0; return; }
    if (!s->ready) {
        s->ready = 1; s->next_due = now;
        if (!s->seed) s->seed = seed ? seed : 1U;
    }
    if (s->pending) {
        if (now - s->sent < 5000U) return;
        s->pending = 0; s->next_due = now;
    }
    if (now < s->next_due) return;
    if (s->attempts == 3U) {
        int ok = s->have_best && p->apply(p->context, s->best_utc, s->best_error,
                                          s->best_sent, s->best_received);
        s->attempts = s->have_best = 0;
        /* Keep half the remaining error budget for slew, then refresh before
         * half of the remaining drift budget is consumed. */
        uint64_t interval = 30000ULL;
        if (ok) {
            uint32_t bound = s->best_error + (s->best_rtt + 1U) / 2U + 1U;
            interval = (uint64_t)(ABOX_CLOCK_ERROR_LIMIT_MS - bound) * 1000000U /
                       (4U * ABOX_CLOCK_DRIFT_PPM);
            if (interval > 1800000ULL) interval = 1800000ULL;
            if (interval < 30000ULL) interval = 30000ULL;
        }
        s->next_due = now + interval;
        return;
    }
    /* newlib-nano has no long-long printf format support. */
    snprintf(s->request_id,sizeof(s->request_id),"ts-%08lx%08lx-%lx",
             (unsigned long)(uint32_t)(s->seed >> 32),
             (unsigned long)(uint32_t)s->seed,(unsigned long)++s->sequence);
    s->sent = now; s->pending = 1;
    if (!p->send(p->context,s->request_id)) {
        s->pending = 0; s->next_due = now + 1000U;
        return;
    }
    ++s->attempts;
}
int ABoxTimeSync_Response(ABoxTimeSync *s, const char *id, uint64_t utc,
                            uint32_t error, uint64_t received)
{
    uint64_t rtt, candidate, previous, difference;
    if (!s || !id || !s->pending || strcmp(id,s->request_id)) return 0;
    if (utc < ABOX_CLOCK_MIN_UTC_MS || utc >= ABOX_CLOCK_MAX_UTC_MS ||
        !ABoxClock_ReferenceErrorMs(error, s->sent, received)) return 0;
    rtt = received - s->sent;
    s->pending = 0; s->next_due = received + 1000U;
    if (s->have_best) {
        candidate = utc + rtt / 2U;
        previous = s->best_utc + s->best_rtt / 2U + received - s->best_received;
        difference = candidate > previous ? candidate - previous : previous - candidate;
        if (difference > 500U) { /* inconsistent batch: retry, never step UTC */
            s->have_best = 0; s->attempts = 3U;
            return 0;
        }
    }
    if (!s->have_best || rtt < s->best_rtt) {
        s->have_best = 1; s->best_rtt = (uint32_t)rtt;
        s->best_sent = s->sent; s->best_received = received;
        s->best_utc = utc; s->best_error = error;
    }
    return 1;
}
