#ifndef ABOX_ADC_H
#define ABOX_ADC_H
#include <stdint.h>
#include "abox_hal_status.h"
#include "abox_dma.h"

/* Physical ADC1/ADC2. Both share the ADC clock and dual-mode controls, so
 * reserve both controllers and their common clock against other code. */
typedef struct { uint8_t clock_divider; } ABoxAdcConfig; /* APB2 / 2,4,6,8; GD32 also 12,16 */
typedef enum {
    ABOX_ADC_SAMPLE_1_5, ABOX_ADC_SAMPLE_7_5, ABOX_ADC_SAMPLE_13_5,
    ABOX_ADC_SAMPLE_28_5, ABOX_ADC_SAMPLE_41_5, ABOX_ADC_SAMPLE_55_5,
    ABOX_ADC_SAMPLE_71_5, ABOX_ADC_SAMPLE_239_5
} ABoxAdcSampleTime;

/* Open validates the current APB2 clock, initializes and calibrates. It
 * requires a bound, advancing millisecond tick. External GPIO must already
 * be configured as analog. Calls on the shared ADC block are serialized. */
ABoxHalStatus ABoxAdc_Open(unsigned instance, const ABoxAdcConfig *config);
ABoxHalStatus ABoxAdc_Close(unsigned instance);
typedef struct {
    uint16_t low,high; /* inclusive 12-bit thresholds */
    uint8_t regular,injected; /* monitor either or both groups */
    uint8_t single_channel,channel; /* otherwise monitor every converted channel */
    uint8_t interrupt;
} ABoxAdcWatchConfig;
typedef struct { void *context; void (*out_of_range)(void *context,unsigned instance); } ABoxAdcWatchEvents;
/* The analog watchdog monitors converted codes, including during DMA or an
 * injected scan. Configure it while the ADC is idle; it remains configured
 * until explicitly disabled, including across conversion modes. In all-channel
 * mode the hardware flag does not identify which channel violated a threshold.
 * For interrupt mode the board configures the shared ADC IRQ and dispatches
 * ABoxAdc_Irq for both instances. The event table/context remain valid until
 * disable; callbacks run in IRQ context and must not reenter this ADC. The
 * threshold flag can recur on subsequent conversions. Close returns BUSY until
 * monitoring is disabled. These are digital codes, not verified voltages. */
ABoxHalStatus ABoxAdc_WatchConfigure(unsigned instance,const ABoxAdcWatchConfig *config,
                                      const ABoxAdcWatchEvents *events);
ABoxHalStatus ABoxAdc_WatchRead(unsigned instance,uint8_t *pending);
ABoxHalStatus ABoxAdc_WatchClear(unsigned instance);
ABoxHalStatus ABoxAdc_WatchDisable(unsigned instance);
void ABoxAdc_Irq(unsigned instance);
/* Read returns a right-aligned 12-bit sample. Channels 0..15 are external;
 * channel 16 is temperature and 17 is Vref, available only on ADC1 with
 * 239.5-cycle sampling. A timeout/error can leave the converter in an
 * uncertain state: Close/Open it before another conversion. `value` is only
 * written on success. Counts require board-specific voltage/calibration data
 * before conversion to physical units. */
ABoxHalStatus ABoxAdc_Read(unsigned instance, unsigned channel, ABoxAdcSampleTime sample,
                           uint32_t timeout_ms, uint16_t *value);
typedef struct { uint16_t adc1,adc2; } ABoxAdcDualSample;
/* One software-triggered, simultaneous regular conversion on ADC1 and ADC2.
 * Both must be open and idle; their sample time is shared to align conversions.
 * Select distinct channels: the same channel cannot be sampled simultaneously
 * by both converters on all supported MCUs.
 * The caller owns both ADCs and their common clock. The result is written only
 * on success. A timeout or hardware error requires closing and reopening both
 * ADCs. This is a single conversion, not a dual-mode DMA stream. */
ABoxHalStatus ABoxAdc_ReadDual(unsigned adc1_channel,unsigned adc2_channel,ABoxAdcSampleTime sample,
                               uint32_t timeout_ms,ABoxAdcDualSample *value);
typedef struct { uint8_t channel; ABoxAdcSampleTime sample; } ABoxAdcSequenceEntry;
typedef struct { uint8_t adc1_channel,adc2_channel; ABoxAdcSampleTime sample; } ABoxAdcDualEntry;
typedef enum { ABOX_ADC_INJ_TIM1_TRGO, ABOX_ADC_INJ_TIM1_CC4, ABOX_ADC_INJ_TIM2_TRGO,
               ABOX_ADC_INJ_TIM2_CC1, ABOX_ADC_INJ_TIM3_CC4, ABOX_ADC_INJ_TIM4_TRGO,
               ABOX_ADC_INJ_EXTI15, ABOX_ADC_INJ_SOFTWARE } ABoxAdcInjectedTrigger;
typedef struct {
    const ABoxAdcSequenceEntry *entries;
    uint8_t entry_count; /* 1..4 conversions */
    ABoxAdcInjectedTrigger trigger;
} ABoxAdcInjectedConfig;
/* Exclusive injected scan on ADC1 or ADC2. Configure the board's analog pins
 * and reserve the converter before Start. With an external trigger, keep the
 * trigger source stopped until Start returns and issue exactly one event before
 * Finish; additional triggers can overwrite the conversion registers. Software
 * triggering starts immediately. Ready is nonblocking. Finish waits at most
 * timeout_ms, writes exactly entry_count right-aligned samples only on success,
 * and restores the ordinary one-shot configuration. Timeout or Abort requires
 * Close/Open. All ADC operations are serialized, and the advancing platform
 * tick must not be blocked during Finish. DMA and injection do not overlap. */
ABoxHalStatus ABoxAdc_StartInjected(unsigned instance,const ABoxAdcInjectedConfig *config);
ABoxHalStatus ABoxAdc_InjectedReady(unsigned instance,uint8_t *ready);
ABoxHalStatus ABoxAdc_FinishInjected(unsigned instance,uint32_t timeout_ms,uint16_t *samples,uint8_t capacity);
ABoxHalStatus ABoxAdc_AbortInjected(unsigned instance);
typedef enum { ABOX_ADC_DUAL_INJECT_SIMULTANEOUS, ABOX_ADC_DUAL_INJECT_ALTERNATE } ABoxAdcDualInjectionMode;
typedef struct {
    const ABoxAdcDualEntry *entries;
    uint8_t entry_count; /* 1..4 paired injected conversions */
    ABoxAdcInjectedTrigger trigger;
    ABoxAdcDualInjectionMode mode; /* zero selects simultaneous */
} ABoxAdcDualInjectedConfig;
/* Zero-initialize the entire configuration before assigning its fields.
 * Injected sequence on both opened ADCs. Each pair uses the same sampling time;
 * simultaneous channels must differ. ADC1 supplies both trigger sequences.
 * Alternate mode converts ADC1 and ADC2 on consecutive triggers; for software
 * triggering, Start issues the first and Advance issues the second after the
 * first completes. External triggers must stay stopped until Start returns and
 * occur only once in simultaneous mode or twice in alternate mode
 * before Finish; another conversion can overwrite results. Ready is nonblocking;
 * Finish writes entry_count pairs only on success. Abort or timeout requires
 * Close/Open both ADCs. The owner serializes both converters and their clock. */
ABoxHalStatus ABoxAdc_StartDualInjected(const ABoxAdcDualInjectedConfig *config);
ABoxHalStatus ABoxAdc_DualInjectedReady(uint8_t *ready);
ABoxHalStatus ABoxAdc_AdvanceDualInjected(void);
ABoxHalStatus ABoxAdc_FinishDualInjected(uint32_t timeout_ms,ABoxAdcDualSample *samples,uint8_t capacity);
ABoxHalStatus ABoxAdc_AbortDualInjected(void);
typedef enum { ABOX_ADC_TRIG_TIM1_CC1, ABOX_ADC_TRIG_TIM1_CC2, ABOX_ADC_TRIG_TIM1_CC3,
               ABOX_ADC_TRIG_TIM2_CC2, ABOX_ADC_TRIG_TIM3_TRGO, ABOX_ADC_TRIG_TIM4_CC4,
               ABOX_ADC_TRIG_EXTI11, ABOX_ADC_TRIG_SOFTWARE } ABoxAdcTrigger;
typedef enum { ABOX_ADC_INJECT_AFTER_REGULAR, ABOX_ADC_INJECT_TRIGGERED } ABoxAdcLinkMode;
typedef struct {
    void *context;
    /* Values are valid only for this IRQ callback; copy them if needed. */
    void (*on_samples)(void *context,unsigned instance,const uint16_t *values,uint8_t count);
} ABoxAdcLinkedEvents;
typedef struct {
    const ABoxAdcSequenceEntry *entries;
    uint8_t entry_count; /* 1..4 */
    ABoxAdcLinkMode mode;
    /* AFTER_REGULAR requires SOFTWARE here. TRIGGERED accepts any injected
     * trigger, including an explicit ABoxAdc_TriggerLinkedInjected call. */
    ABoxAdcInjectedTrigger trigger;
    const ABoxAdcLinkedEvents *events;
} ABoxAdcLinkedConfig;
typedef struct {
    const ABoxAdcSequenceEntry *entries;
    uint8_t entry_count; /* 1..16 regular conversions per scan */
    ABoxAdcTrigger trigger;
    uint8_t continuous; /* software trigger only; otherwise one scan per trigger */
    uint8_t circular;
    const ABoxAdcLinkedConfig *linked; /* optional injected group */
} ABoxAdcDmaConfig;
typedef struct {
    void *context;
    void (*on_event)(void *context, ABoxDmaEvent event);
} ABoxAdcDmaEvents;
/* ADC1 regular scan to DMA1 channel 1; ADC2 has no independent regular DMA
 * request on these MCUs. Buffer length must cover whole scans; circular mode
 * uses an even number of scans so half events never split a scan. Open and
 * calibrate ADC first. For external trigger, keep the source stopped until
 * StartDma returns, then start it separately. For software/non-continuous,
 * count equals one scan. Events and aligned sample storage remain valid until
 * StopDma; callbacks run in IRQ context and cannot reenter this ADC. The
 * normal completion callback pauses further requests; StopDma always releases
 * the channel and restores the one-shot Read configuration. An error requires
 * StopDma before reuse. For linked injection, the caller also configures and
 * dispatches the shared ADC IRQ via ABoxAdc_Irq. Regular and injected entries
 * using the same channel must use the same sample time. Stop external trigger
 * sources before stopping. In automatic mode a non-circular completed DMA can
 * precede its last injected conversion: StopDma returns BUSY until all expected
 * injected IRQs have been observed, and AbortDma can discard unfinished data.
 * Circular/early Stop may discard an in-flight conversion. Injected registers
 * hold only the latest sequence; if another sequence completes before the IRQ
 * is serviced, hardware does not report the overwrite. Select trigger rates
 * and interrupt latency accordingly. Counts are uncalibrated 12-bit samples. */
ABoxHalStatus ABoxAdc_StartDma(unsigned instance,const ABoxAdcDmaConfig *config,
                               uint16_t *samples,uint16_t count,const ABoxAdcDmaEvents *events);
ABoxHalStatus ABoxAdc_StopDma(unsigned instance);
/* Abort may discard a conversion and requires Close/Open before reuse. */
ABoxHalStatus ABoxAdc_AbortDma(unsigned instance);
/* Only for TRIGGERED + SOFTWARE; returns BUSY until the previous software
 * injection has been dispatched. The caller avoids overlap with external
 * events and must account for injection interrupting regular conversions. */
ABoxHalStatus ABoxAdc_TriggerLinkedInjected(unsigned instance);
/* Obtain the latest IRQ-dispatched injected sequence and its wrapping
 * generation number. Returns BUSY before the first observed completion;
 * output is not modified on failure. It cannot detect hardware overwrites. */
ABoxHalStatus ABoxAdc_GetLinkedInjected(unsigned instance,uint16_t *samples,uint8_t capacity,uint32_t *generation);

typedef enum { ABOX_ADC_DUAL_SIMULTANEOUS, ABOX_ADC_DUAL_INTERLEAVED_FAST,
               ABOX_ADC_DUAL_INTERLEAVED_SLOW } ABoxAdcDualMode;
typedef struct {
    void *context;
    void (*on_samples)(void *context,const ABoxAdcDualSample *values,uint8_t count);
} ABoxAdcDualLinkedEvents;
typedef struct {
    const ABoxAdcDualEntry *entries;
    uint8_t entry_count; /* 1..4 paired injected conversions */
    ABoxAdcInjectedTrigger trigger;
    const ABoxAdcDualLinkedEvents *events;
    ABoxAdcDualInjectionMode mode; /* zero selects simultaneous */
    uint8_t automatic; /* simultaneous regular/injected only; trigger must be SOFTWARE */
} ABoxAdcDualLinkedConfig;
typedef struct {
    const ABoxAdcDualEntry *entries;
    uint8_t entry_count; /* 1..16 paired conversions per scan */
    ABoxAdcTrigger trigger;
    uint8_t continuous,circular;
    ABoxAdcDualMode mode;
    const ABoxAdcDualLinkedConfig *linked; /* optional injected group */
} ABoxAdcDualDmaConfig;
/* Paired regular conversion on both opened ADCs to DMA1 channel
 * 1. Each aligned uint32_t is ADC1 in bits 0..15 and ADC2 in bits 16..31;
 * count is the number of pairs, not bytes. Each pair uses one sampling time
 * for both ADCs. Simultaneous mode requires distinct channels in each pair.
 * Fast and slow interleaved modes require one entry with the same external
 * channel on both ADCs; the upper halfword (ADC2) is sampled before the lower
 * halfword (ADC1). Fast mode permits 1.5-cycle sampling; slow permits 1.5,
 * 7.5 or 13.5 cycles. In slow mode continuous must be zero: hardware converts
 * continuously after the first trigger. In fast mode continuous controls both
 * ADCs and may follow either a software or external initial trigger.
 * Count covers whole scans and circular half boundaries cover whole scans.
 * Software/non-continuous performs exactly one scan except in slow mode.
 * Reserve both ADCs, their
 * clock, DMA, pins and trigger; start an external trigger only after Start
 * returns. StopDualDma restores independent one-shot mode. DMA error requires
 * StopDualDma and Close/Open both converters. Callbacks and buffer remain valid
 * through StopDualDma and run in IRQ context. Optional linked injected groups
 * work with circular or finite DMA; the board dispatches both ADC IRQs. After
 * finite DMA completes, injected conversions remain enabled until StopDualDma.
 * In slow interleaved mode regular conversions also continue without DMA until
 * StopDualDma. Automatic injection returns BUSY from StopDualDma until all
 * injections corresponding to the finite buffer have been dispatched; use
 * AbortDualDma to abandon missing results and Close/Open both converters.
 * Alternate injection is available with simultaneous regular conversions:
 * each pair needs two triggers, with one completion serviced before the next
 * software trigger. Automatic injection follows each regular scan in the
 * simultaneous/simultaneous mode. A partial alternate pair is discarded on
 * StopDualDma. Initialize the entire configuration, including optional fields,
 * to zero before setting values; older field-by-field initializers need updating.
 * Their callback receives pairs and the latest sequence can be read separately.
 * Stop injected trigger sources and allow in-flight IRQs to finish before
 * StopDualDma, which can otherwise discard an injected conversion. Trigger
 * rates must account for regular conversions being interrupted and for injected
 * registers holding only the latest results. */
ABoxHalStatus ABoxAdc_StartDualDma(const ABoxAdcDualDmaConfig *config,uint32_t *pairs,uint16_t count,
                                   const ABoxAdcDmaEvents *events);
ABoxHalStatus ABoxAdc_StopDualDma(void);
ABoxHalStatus ABoxAdc_AbortDualDma(void);
ABoxHalStatus ABoxAdc_TriggerDualLinkedInjected(void);
ABoxHalStatus ABoxAdc_GetDualLinkedInjected(ABoxAdcDualSample *samples,uint8_t capacity,uint32_t *generation);
#endif
