#ifndef ABOX_CLOCK_H
#define ABOX_CLOCK_H
#include <stdint.h>

typedef enum { ABOX_CLOCK_NONE, ABOX_CLOCK_CCLK, ABOX_CLOCK_PLATFORM } ABoxClockSource;
typedef enum { ABOX_CLOCK_INVALID, ABOX_CLOCK_BOOTSTRAP,
               ABOX_CLOCK_SYNCHRONIZED, ABOX_CLOCK_UNTRUSTED } ABoxClockQuality;
typedef struct {
    uint64_t utc_ms, mono_ms, sync_age_ms;
    uint32_t error_ms;
    ABoxClockSource source;
    ABoxClockQuality quality;
} ABoxClockSnapshot;
typedef struct {
    uint64_t utc_at_sync, mono_at_sync, last_sync_mono;
    int64_t correction_ms;
    uint32_t error_at_sync;
    ABoxClockSource source;
} ABoxClock;

#define ABOX_CLOCK_ERROR_LIMIT_MS 500U
#define ABOX_CLOCK_DRIFT_PPM 100U
#define ABOX_CLOCK_MAX_AGE_MS 7200000ULL
#define ABOX_CLOCK_MAX_RTT_MS 800U
/* Preserve the former worst-case sample bound (250 + 400/2 + 1).
 * Remaining budget covers drift and the existing slew/refresh policy. */
#define ABOX_CLOCK_MAX_SAMPLE_ERROR_MS 451U
#define ABOX_CLOCK_MAX_REFERENCE_ERROR_MS 250U
#define ABOX_CLOCK_MIN_UTC_MS 1704067200000ULL
#define ABOX_CLOCK_MAX_UTC_MS 4102444800000ULL

void ABoxClock_Init(ABoxClock *clock);
ABoxClockSnapshot ABoxClock_Snapshot(const ABoxClock *clock, uint64_t mono_ms);
/* Unknown, bootstrap and expired time must not be advertised as trusted UTC. */
uint64_t ABoxClock_ReportedUtcMs(const ABoxClockSnapshot *snapshot);
/* Zero means unusable; otherwise reference + half RTT + rounding bound. */
uint32_t ABoxClock_ReferenceErrorMs(uint32_t reference_error_ms,
                                  uint64_t sent_mono_ms, uint64_t received_mono_ms);
int ABoxClock_Bootstrap(ABoxClock *clock, uint64_t utc_ms, uint64_t mono_ms);
/* server UTC is sampled immediately before publishing its response. The full
 * client round trip includes all queue/processing delay, conservatively. */
int ABoxClock_Synchronize(ABoxClock *clock, uint64_t server_utc_ms,
                           uint32_t server_error_ms, uint64_t sent_mono_ms,
                           uint64_t received_mono_ms);
/* local_time=1 subtracts the signed quarter-hour suffix. local_time=0 means
 * the identified modem mode supplies UTC calendar fields; still validate TZ. */
int ABoxClock_ParseCCLK(const char *line, uint8_t local_time, uint64_t *utc_ms);
#endif
