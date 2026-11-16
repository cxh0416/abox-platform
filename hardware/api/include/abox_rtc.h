#ifndef ABOX_RTC_H
#define ABOX_RTC_H
#include <stdint.h>
#include "abox_hal_status.h"

typedef enum { ABOX_RTC_LSE=1, ABOX_RTC_LSI=2, ABOX_RTC_HSE_DIV128=3,
               ABOX_RTC_LSE_BYPASS=4 } ABoxRtcSource;
typedef struct {
    ABoxRtcSource source;
    uint32_t source_hz; /* RTC input frequency; HSE divided by 128 */
    uint32_t divider;   /* 1..1048576 input clocks per counter increment */
    uint32_t initial_counter;
    uint8_t initialize_if_empty;
} ABoxRtcConfig;
#define ABOX_RTC_SECOND   1U
#define ABOX_RTC_ALARM    2U
#define ABOX_RTC_OVERFLOW 4U

/* The caller owns and serializes the RTC and backup domain; a bound, advancing
 * platform tick is required for waits. Open attaches to an already configured
 * RTC only if source and divider match. With initialize_if_empty, an unselected
 * domain is initialized to initial_counter. A source mismatch is BUSY: it never
 * resets the backup domain or silently destroys backup registers. LSE_BYPASS
 * uses a 32.768 kHz..1 MHz external signal; changing the bypass mode while LSE
 * is running returns BUSY. A timeout can
 * leave a newly selected source partially configured; recover explicitly. */
ABoxHalStatus ABoxRtc_Open(const ABoxRtcConfig *config, uint32_t timeout_ms);
/* Close masks interrupts configured by this interface, but keeps RTC running. */
ABoxHalStatus ABoxRtc_Close(uint32_t timeout_ms);
/* Values are raw 32-bit counter ticks, not automatically trusted UTC. Reads
 * tolerate a high/low rollover. Writes may affect alarm timing; callers must
 * coordinate alarm/second flags and the reference clock. */
ABoxHalStatus ABoxRtc_Read(uint32_t *counter);
ABoxHalStatus ABoxRtc_Write(uint32_t counter, uint32_t timeout_ms);
ABoxHalStatus ABoxRtc_SetAlarm(uint32_t counter, uint32_t timeout_ms);
ABoxHalStatus ABoxRtc_GetFlags(uint8_t *flags);
ABoxHalStatus ABoxRtc_ClearFlags(uint8_t flags, uint32_t timeout_ms);
ABoxHalStatus ABoxRtc_SetInterrupts(uint8_t mask, uint32_t timeout_ms);
typedef struct {
    void *context;
    void (*on_events)(void *context, uint8_t flags);
} ABoxRtcEvents;
/* Dispatch enabled, pending SECOND/ALARM/OVERFLOW flags in task context.
 * Callback runs before acknowledgement; on TIMEOUT it may be called again for
 * the same flag, so the receiver must tolerate duplicates. Callbacks must not
 * reenter RTC operations. The owner serializes RTC and backup-domain access.
 * This function may wait for the platform tick and is not an IRQ handler:
 * an ISR must mask/defer the source and the board must separately acknowledge
 * any alarm EXTI route before unmasking it. Multiple occurrences of one flag
 * may coalesce in hardware; the flags are not event counters. */
ABoxHalStatus ABoxRtc_DispatchEvents(const ABoxRtcEvents *events,uint32_t timeout_ms);
/* F105 calibration removes `pulses` input clocks per 2^20 RTC clocks;
 * valid values are 0..127, and the correction only slows the counter. The
 * caller measures the reference and owns backup-domain access coordination.
 * This does not change RTC output selection or source.
 * The calibration persists in the backup domain across ordinary Close/Open. */
ABoxHalStatus ABoxRtc_SetCalibration(uint8_t pulses, uint32_t timeout_ms);
ABoxHalStatus ABoxRtc_GetCalibration(uint8_t *pulses);
typedef enum { ABOX_RTC_OUTPUT_OFF,ABOX_RTC_OUTPUT_CALIBRATION,
               ABOX_RTC_OUTPUT_ALARM,ABOX_RTC_OUTPUT_SECOND } ABoxRtcOutput;
/* Select the backup-domain RTC signal output. Calibration outputs RTCCLK/64;
 * alarm/second are one RTC input-clock pulse. Selection persists across normal
 * RTC Close/Open. This changes only the output multiplexer, not calibration,
 * RTC source, GPIO setup, or backup-domain reset. Coordinate all accesses to
 * the shared backup control register. Get reports the signal selected by the
 * hardware, including alarm/second priority if multiple bits were set. */
ABoxHalStatus ABoxRtc_SetOutput(ABoxRtcOutput output,uint32_t timeout_ms);
ABoxHalStatus ABoxRtc_GetOutput(ABoxRtcOutput *output);
/* Explicitly destructive: RTC counter, prescaler, alarm and every backup
 * register are reset. Only allowed while closed; never called implicitly. */
ABoxHalStatus ABoxRtc_ResetBackupDomain(uint32_t timeout_ms);
#endif
