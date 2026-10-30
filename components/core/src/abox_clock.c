#include "abox_clock.h"
#include <stdio.h>
#include <string.h>

static uint64_t magnitude(int64_t n) { return (uint64_t)(n < 0 ? -n : n); }
static int leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
static int days(int y, int m)
{
    static const uint8_t d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return d[m - 1] + (m == 2 && leap(y));
}
void ABoxClock_Init(ABoxClock *c) { if (c) memset(c, 0, sizeof(*c)); }
uint64_t ABoxClock_ReportedUtcMs(const ABoxClockSnapshot *s)
{ return s && s->quality == ABOX_CLOCK_SYNCHRONIZED ? s->utc_ms : 0U; }
ABoxClockSnapshot ABoxClock_Snapshot(const ABoxClock *c, uint64_t mono)
{
    ABoxClockSnapshot s = {0};
    uint64_t elapsed, error, adjustment;
    int64_t correction;
    s.mono_ms = mono;
    if (!c || c->source == ABOX_CLOCK_NONE || mono < c->mono_at_sync) return s;
    elapsed = mono - c->mono_at_sync;
    /* Slew at at most 1 ms/s. Negative corrections cannot reverse UTC. */
    adjustment = elapsed / 1000U;
    if (adjustment > magnitude(c->correction_ms)) adjustment = magnitude(c->correction_ms);
    correction = c->correction_ms < 0 ? -(int64_t)adjustment : (int64_t)adjustment;
    s.utc_ms = (uint64_t)((int64_t)(c->utc_at_sync + elapsed) + correction);
    s.sync_age_ms = mono - c->last_sync_mono;
    error = c->error_at_sync + (s.sync_age_ms / 1000000U) * ABOX_CLOCK_DRIFT_PPM +
            ((s.sync_age_ms % 1000000U) * ABOX_CLOCK_DRIFT_PPM + 999999U) / 1000000U +
            magnitude(c->correction_ms - correction);
    s.error_ms = error > UINT32_MAX ? UINT32_MAX : (uint32_t)error;
    s.source = c->source;
    s.quality = c->source == ABOX_CLOCK_CCLK ? ABOX_CLOCK_BOOTSTRAP :
        (s.sync_age_ms <= ABOX_CLOCK_MAX_AGE_MS && error <= ABOX_CLOCK_ERROR_LIMIT_MS ?
         ABOX_CLOCK_SYNCHRONIZED : ABOX_CLOCK_UNTRUSTED);
    return s;
}
int ABoxClock_Bootstrap(ABoxClock *c, uint64_t utc, uint64_t mono)
{
    if (!c || c->source == ABOX_CLOCK_PLATFORM || utc < ABOX_CLOCK_MIN_UTC_MS ||
        utc >= ABOX_CLOCK_MAX_UTC_MS) return 0;
    c->utc_at_sync = utc; c->mono_at_sync = c->last_sync_mono = mono;
    c->error_at_sync = 2000U; c->correction_ms = 0;
    c->source = ABOX_CLOCK_CCLK;
    return 1;
}
int ABoxClock_Synchronize(ABoxClock *c, uint64_t server, uint32_t error,
                           uint64_t sent, uint64_t received)
{
    ABoxClockSnapshot old;
    uint64_t rtt, target;
    int64_t offset;
    if (!c || received < sent || server < ABOX_CLOCK_MIN_UTC_MS ||
        server >= ABOX_CLOCK_MAX_UTC_MS) return 0;
    rtt = received - sent;
    if (rtt > ABOX_CLOCK_MAX_RTT_MS || error > ABOX_CLOCK_MAX_REFERENCE_ERROR_MS) return 0;
    target = server + rtt / 2U;
    old = ABoxClock_Snapshot(c, received);
    offset = (int64_t)target - (int64_t)old.utc_ms;
    c->utc_at_sync = target;
    c->correction_ms = 0;
    if (old.quality == ABOX_CLOCK_SYNCHRONIZED && magnitude(offset) <= 200U &&
        magnitude(offset) <= (ABOX_CLOCK_ERROR_LIMIT_MS -
            (error + (uint32_t)((rtt + 1U) / 2U) + 1U)) / 2U) {
        c->utc_at_sync = old.utc_ms;
        c->correction_ms = offset;
    }
    c->mono_at_sync = c->last_sync_mono = received;
    c->error_at_sync = error + (uint32_t)((rtt + 1U) / 2U) + 1U;
    c->source = ABOX_CLOCK_PLATFORM;
    return 1;
}
int ABoxClock_ParseCCLK(const char *line, uint8_t local, uint64_t *utc)
{
    int yy, m, d, h, min, sec, tz, used = 0, y, i;
    char sign;
    int64_t total = 0;
    const char *p;
    if (!line || !utc || !(p = strstr(line, "+CCLK: \""))) return 0;
    p += 8;
    if (strlen(p) != 21U || p[2] != '/' || p[5] != '/' || p[8] != ',' ||
        p[11] != ':' || p[14] != ':' || p[20] != '"') return 0;
    for (i = 0; i < 20; ++i)
        if (i != 2 && i != 5 && i != 8 && i != 11 && i != 14 && i != 17 &&
            (p[i] < '0' || p[i] > '9')) return 0;
    if (sscanf(p, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d\"%n",
               &yy, &m, &d, &h, &min, &sec, &sign, &tz, &used) != 8 || used != 21 ||
        (sign != '+' && sign != '-') || tz > (sign == '-' ? 48 : 56)) return 0;
    y = 2000 + yy;
    if (y < 2024 || m < 1 || m > 12 || d < 1 || d > days(y,m) ||
        h > 23 || min > 59 || sec > 59) return 0;
    for (i = 1970; i < y; ++i) total += leap(i) ? 366 : 365;
    for (i = 1; i < m; ++i) total += days(y,i);
    total = (total + d - 1) * 86400 + h * 3600 + min * 60 + sec;
    if (local) total -= (sign == '-' ? -tz : tz) * 900;
    if (total * 1000 < (int64_t)ABOX_CLOCK_MIN_UTC_MS ||
        total * 1000 >= (int64_t)ABOX_CLOCK_MAX_UTC_MS) return 0;
    *utc = (uint64_t)total * 1000U;
    return 1;
}
