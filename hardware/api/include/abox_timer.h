#ifndef ABOX_TIMER_H
#define ABOX_TIMER_H
#include <stdint.h>
#include "abox_hal_status.h"

/* Instances 1..7 denote physical TIM1..TIM7; 6 and 7 are basic timers.
 * Reserve timers against board timebases and every other user before Open. */
typedef enum { ABOX_TIMER_UP, ABOX_TIMER_DOWN, ABOX_TIMER_CENTER_DOWN,
               ABOX_TIMER_CENTER_UP, ABOX_TIMER_CENTER_BOTH } ABoxTimerMode;
typedef struct {
    uint32_t prescaler; /* timer input clock divisor, 1..65536 */
    uint32_t period;    /* counter ticks, 1..65536 */
    ABoxTimerMode mode;
} ABoxTimerConfig;
typedef struct {
    void *context;
    void (*update)(void *context, unsigned instance);
} ABoxTimerEvents;

/* Owner configures NVIC and dispatches ABoxTimer_Irq. Events and context must
 * live until Stop; callback runs in IRQ context and must not reenter this
 * timer. Hardware pins and alternate functions remain the board's duty. */
ABoxHalStatus ABoxTimer_Open(unsigned instance, const ABoxTimerConfig *config);
ABoxHalStatus ABoxTimer_Start(unsigned instance, const ABoxTimerEvents *events);
/* Disable all PWM outputs before stopping or closing the counter. */
ABoxHalStatus ABoxTimer_Stop(unsigned instance);
ABoxHalStatus ABoxTimer_Close(unsigned instance);
/* Reset all registers of a closed timer, including its DMA-burst index. */
ABoxHalStatus ABoxTimer_Reset(unsigned instance);
ABoxHalStatus ABoxTimer_Read(unsigned instance, uint32_t *counter);
ABoxHalStatus ABoxTimer_SetCounter(unsigned instance, uint32_t counter);
/* Configure one-pulse counter operation while stopped. On the next update
 * event the hardware clears its counter enable bit. Software still owns the
 * running state: disable outputs/capture, call Stop, set the desired counter
 * value and Start again to rearm. A slave trigger can start it again after an
 * update until Stop disarms the trigger; do not assume one trigger per Start.
 * For deterministic pulse timing use edge-aligned counting and configure the
 * output, trigger, counter and safe pin state explicitly. TIM6/7 have no
 * output channels. One-pulse completion does not establish an external edge. */
ABoxHalStatus ABoxTimer_SetOnePulse(unsigned instance, uint8_t enabled);
/* TIM1 repetition counter: 0 generates an update on every overflow or
 * underflow; N generates one update per N+1 counter periods in edge-aligned
 * mode, or half-periods in center-aligned mode. Set while stopped. Loading
 * generates a software update, resets the counter and can produce TRGO or
 * other update effects; prepare dependent peripherals accordingly. The
 * register is 8-bit. Other F105 timers return UNSUPPORTED. */
ABoxHalStatus ABoxTimer_SetRepetition(unsigned instance,uint8_t repetition);
/* TRGO is shared by all users of a timer and is configured while stopped.
 * Basic timers 6/7 support RESET, ENABLE and UPDATE only. Compare pulse and
 * OCxREF modes require a general/advanced timer and the matching channel. */
typedef enum { ABOX_TIMER_TRGO_RESET, ABOX_TIMER_TRGO_ENABLE, ABOX_TIMER_TRGO_UPDATE,
               ABOX_TIMER_TRGO_COMPARE_PULSE, ABOX_TIMER_TRGO_OC1,
               ABOX_TIMER_TRGO_OC2, ABOX_TIMER_TRGO_OC3, ABOX_TIMER_TRGO_OC4 } ABoxTimerTrigger;
ABoxHalStatus ABoxTimer_SetTrigger(unsigned instance, ABoxTimerTrigger trigger);
/* Select capture/compare DMA requests on compare events (0) or timer update
 * (1). This setting is shared by all four channels of TIM1..5. Configure it
 * while the timer and its DMA streams are stopped. */
ABoxHalStatus ABoxTimer_SetCaptureDmaOnUpdate(unsigned instance,uint8_t enabled);
typedef enum { ABOX_TIMER_SLAVE_DISABLED=0, ABOX_TIMER_SLAVE_RESET=4, ABOX_TIMER_SLAVE_GATED=5,
               ABOX_TIMER_SLAVE_TRIGGER=6, ABOX_TIMER_EXTERNAL_CLOCK_1=7,
               ABOX_TIMER_EXTERNAL_CLOCK_2=8 } ABoxTimerSlaveMode;
typedef enum { ABOX_TIMER_ITR0, ABOX_TIMER_ITR1, ABOX_TIMER_ITR2, ABOX_TIMER_ITR3,
               ABOX_TIMER_TI1_EDGE, ABOX_TIMER_TI1_FILTERED, ABOX_TIMER_TI2_FILTERED,
               ABOX_TIMER_ETR } ABoxTimerInputTrigger;
typedef struct {
    ABoxTimerSlaveMode mode;
    ABoxTimerInputTrigger source;
    uint8_t etr_prescaler; /* 1, 2, 4 or 8 */
    uint8_t etr_filter;    /* hardware filter code 0..15 */
    uint8_t etr_inverted;  /* active-low or falling ETR */
} ABoxTimerSlaveConfig;
/* TIM1..TIM5 support reset/gated/trigger synchronization and external clock
 * modes. Configure while stopped and not used by the encoder. ITR routes are
 * timer- and chip-specific; reserve and configure their source separately.
 * For TI1/TI2, configure the associated direct capture input and its filter
 * and polarity; ETR has its own filter/prescaler in this structure. External
 * clock mode 2 uses ETR regardless of source. In trigger mode Start arms the
 * hardware and the selected event starts the counter; Stop disarms it.
 * Reserve the timer against board timebases and consider the resulting PWM,
 * DMA and update rates before enabling outputs. */
ABoxHalStatus ABoxTimer_SetSlave(unsigned instance,const ABoxTimerSlaveConfig *config);
/* TIM1..TIM5 can XOR their three channel inputs into TI1 for a Hall sensor.
 * Set while stopped and with no active capture or encoder. GPIO for all three
 * Hall inputs remains board-owned. This only selects the input: configure the
 * TI1 edge detector, slave reset/trigger, capture and any delayed output with
 * the ordinary timer APIs as appropriate for the motor. Disable it before
 * configuring a quadrature encoder. A Hall edge does not prove rotor position
 * or safe commutation; the application validates the three input states. */
ABoxHalStatus ABoxTimer_SetHallInput(unsigned instance,uint8_t enabled);
void ABoxTimer_Irq(unsigned instance);

/* Channels 1..4 on TIM1..TIM5. A PWM output is configured while disabled.
 * Pulse is the compare threshold from 0 through period, with 0 and period
 * giving constant-inactive and constant-active output. In center-aligned and
 * down-counting modes the edge placement follows the counter direction; the
 * caller must account for that when deriving the frequency and duty cycle.
 * The period is shared by every channel on a timer. Enabling an output can drive a pin;
 * external power and safe idle behavior are the caller's responsibility. */
ABoxHalStatus ABoxPwm_Configure(unsigned instance, unsigned channel,
                               uint32_t pulse, uint8_t active_low);
ABoxHalStatus ABoxPwm_Enable(unsigned instance, unsigned channel);
ABoxHalStatus ABoxPwm_Disable(unsigned instance, unsigned channel);
/* Release a disabled channel before configuring it for capture. */
ABoxHalStatus ABoxPwm_Release(unsigned instance, unsigned channel);

typedef enum { ABOX_COMPARE_FROZEN, ABOX_COMPARE_SET_ACTIVE,
               ABOX_COMPARE_SET_INACTIVE, ABOX_COMPARE_TOGGLE } ABoxCompareMode;
typedef struct {
    void *context;
    void (*matched)(void *context,unsigned instance,unsigned channel);
} ABoxCompareEvents;
/* Output compare reserves one channel of TIM1..5, sharing the timer counter.
 * A match occurs when CNT equals compare (0..period-1); edge placement follows
 * the counter mode. FROZEN reports matches without changing OCREF. The other
 * modes set, clear or toggle OCREF, with active_low controlling output polarity.
 * Configure while the channel is disabled; the caller owns GPIO and safe output
 * state. Start enables the output and optional IRQ; Timer_Start is separate.
 * Without events, Poll clears and reports the pending flag. With events, IRQ
 * callbacks run in IRQ context and cannot reenter this timer. Matches can
 * coalesce; they are not counted. Stop before Release or stopping the timer. */
ABoxHalStatus ABoxCompare_Configure(unsigned instance,unsigned channel,uint32_t compare,
                                   ABoxCompareMode mode,uint8_t active_low);
ABoxHalStatus ABoxCompare_Start(unsigned instance,unsigned channel,const ABoxCompareEvents *events);
ABoxHalStatus ABoxCompare_Poll(unsigned instance,unsigned channel,uint8_t *matched);
ABoxHalStatus ABoxCompare_Stop(unsigned instance,unsigned channel);
ABoxHalStatus ABoxCompare_Release(unsigned instance,unsigned channel);

typedef struct {
    uint8_t dead_time_code; /* raw F105 DTG encoding, in tDTS units */
    uint8_t break_enable, break_active_high, auto_rearm;
    uint8_t off_state_idle, off_state_run;
} ABoxTimerBreakConfig;
/* TIM1 only. Configure with the counter stopped and every output disabled.
 * DTG is non-linear: 0..127 = code*tDTS; 128..191 = (64+low6)*2*tDTS;
 * 192..223 = (32+low5)*8*tDTS; 224..255 = (32+low5)*16*tDTS. The timer
 * uses clock division 1; dead time, break pin polarity, idle levels and board
 * power stage must be verified together. auto_rearm permits hardware to
 * reactivate outputs at an update after break is removed; select it explicitly
 * only when that behavior is safe. Break/idle configuration does not prove a
 * safe external power state. */
ABoxHalStatus ABoxTimer_ConfigureBreak(unsigned instance,const ABoxTimerBreakConfig *config);
ABoxHalStatus ABoxTimer_BreakPending(unsigned instance,uint8_t *pending);
ABoxHalStatus ABoxTimer_ClearBreak(unsigned instance);
typedef struct { void *context;void (*detected)(void *context,unsigned instance); } ABoxTimerBreakEvents;
/* TIM1 break interrupt. The board dispatches its break vector to Timer_Irq.
 * An event masks further break IRQs while leaving the hardware break flag set;
 * ClearBreak acknowledges it and rearms the IRQ. Pass NULL to disable event
 * delivery. Callback runs in IRQ context and cannot reenter this timer; the
 * event storage remains valid until disabled or Close. Serialize configuration
 * and Close with the IRQ. No callback automatically re-arms PWM outputs. */
ABoxHalStatus ABoxTimer_SetBreakEvents(unsigned instance,const ABoxTimerBreakEvents *events);
ABoxHalStatus ABoxTimer_OutputsArmed(unsigned instance,uint8_t *armed);
/* Clear the latched break first and make the external input inactive. Re-arm
 * can energize every enabled output; BUSY means the hardware did not arm. */
ABoxHalStatus ABoxTimer_RearmOutputs(unsigned instance);
/* TIM1 channels 1..3 support an independent complementary output. Configure
 * its polarity and both idle levels after PWM configuration and before either
 * output is enabled. TIM1 channel 4 can configure main_idle_high only. GPIO
 * remapping and the board's gate-driver interlocks remain the caller's duty. */
ABoxHalStatus ABoxPwm_ConfigureAdvancedOutput(unsigned instance,unsigned channel,
                                             uint8_t main_idle_high,uint8_t complementary_idle_high,
                                             uint8_t complementary_active_low);
ABoxHalStatus ABoxPwm_EnableComplementary(unsigned instance,unsigned channel);
ABoxHalStatus ABoxPwm_DisableComplementary(unsigned instance,unsigned channel);

typedef enum { ABOX_PWM_DMA_HALF, ABOX_PWM_DMA_COMPLETE, ABOX_PWM_DMA_ERROR } ABoxPwmDmaEvent;
typedef struct {
    void *context;
    void (*on_event)(void *context, unsigned instance, unsigned channel, ABoxPwmDmaEvent event);
} ABoxPwmDmaEvents;
/* With the timer stopped and PWM configured but disabled, reserve its update
 * request and stream 16-bit compare values. Initial is active immediately;
 * each update requests the next value, which takes effect at the following
 * update through the compare preload. Only one PWM DMA stream per timer is
 * possible. Center-aligned timers can generate two updates per counter cycle.
 * Values must be <= the timer period; at period 65536 a 16-bit
 * sample cannot represent 100% duty. The caller starts the timer/enables PWM,
 * configures and dispatches DMA IRQs, and preserves the buffer and event table
 * through StopDma. A callback runs in IRQ context and must not reenter this
 * timer. Normal completion or error retains the channel until StopDma, and the
 * output retains its last value; the caller controls any required safe state.
 * If a circular buffer is updated, the caller owns validation and coordination
 * of samples before DMA reads them. */
ABoxHalStatus ABoxPwm_StartDma(unsigned instance, unsigned channel, uint16_t initial,
                               const uint16_t *samples, uint16_t count, uint8_t circular,
                               const ABoxPwmDmaEvents *events);
ABoxHalStatus ABoxPwm_StopDma(unsigned instance, unsigned channel);

typedef enum { ABOX_TIMER_BURST_READ, ABOX_TIMER_BURST_WRITE } ABoxTimerBurstDirection;
typedef enum { ABOX_TIMER_BURST_UPDATE, ABOX_TIMER_BURST_CC1, ABOX_TIMER_BURST_CC2,
               ABOX_TIMER_BURST_CC3, ABOX_TIMER_BURST_CC4, ABOX_TIMER_BURST_TRIGGER,
               ABOX_TIMER_BURST_COM } ABoxTimerBurstRequest;
typedef enum { ABOX_TIMER_BURST_HALF, ABOX_TIMER_BURST_COMPLETE,
               ABOX_TIMER_BURST_ERROR } ABoxTimerBurstEvent;
typedef struct { void *context;void (*on_event)(void *context,unsigned instance,ABoxTimerBurstEvent event); } ABoxTimerBurstEvents;
/* TIM1..5 DMA burst, using the F105 16-bit DMAR window. Base
 * is a timer register's word offset from CR1 (0..17); length is the number of
 * consecutive registers per request. Reserved registers and unsupported
 * timer-specific registers are rejected. For WRITE the buffer supplies register
 * values; for READ it receives them. count is in halfwords and must contain
 * whole bursts; circular mode additionally requires an even number of bursts.
 * A normal half callback is omitted when it would split a burst. Configure the
 * stopped timer and its channels before Start, then start the timer separately.
 * DMA, NVIC, buffer and callbacks follow the same lifecycle as PWM DMA.
 * Raw register writes can change timer operation and invalidate cached channel
 * settings: after a write stream, stop outputs/timer and Close/Open before using
 * the high-level timer API again. A stopped or aborted stream may contain a
 * partial burst; after Close use ABoxTimer_Reset before reusing that timer in
 * that case. Callbacks run in IRQ context and must not reenter the same timer. */
ABoxHalStatus ABoxTimer_StartBurstDma(unsigned instance,uint8_t base,uint8_t length,
                                      ABoxTimerBurstDirection direction,uint16_t *words,uint16_t count,
                                      uint8_t circular,const ABoxTimerBurstEvents *events);
/* The original StartBurstDma selects UPDATE. This variant selects UPDATE,
 * CC1..4, TRIGGER or COM where that timer's request has a DMA channel;
 * unsupported mappings return UNSUPPORTED. CC request timing also depends on
 * the timer's capture/compare DMA selection (CCDS) configuration. */
ABoxHalStatus ABoxTimer_StartBurstDmaOn(unsigned instance,ABoxTimerBurstRequest request,uint8_t base,uint8_t length,
                                        ABoxTimerBurstDirection direction,uint16_t *words,uint16_t count,
                                        uint8_t circular,const ABoxTimerBurstEvents *events);
ABoxHalStatus ABoxTimer_StopBurstDma(unsigned instance,uint8_t *partial);

typedef enum { ABOX_CAPTURE_RISING, ABOX_CAPTURE_FALLING } ABoxCaptureEdge;
typedef enum { ABOX_CAPTURE_DIRECT, ABOX_CAPTURE_INDIRECT, ABOX_CAPTURE_TRIGGER } ABoxCaptureInput;
typedef struct {
    ABoxCaptureEdge edge;
    ABoxCaptureInput input;
    uint8_t prescaler; /* capture every 1, 2, 4 or 8 events */
    uint8_t filter;    /* hardware digital filter code 0..15 */
} ABoxCaptureConfig;
typedef struct {
    void *context;
    void (*captured)(void *context, unsigned instance, unsigned channel,
                     uint32_t counter, uint8_t overcapture);
} ABoxCaptureEvents;
/* Channels 1..4 of timers 1..5 share the timer period with PWM. Configure a
 * disabled, unowned channel, then Start it; the caller separately starts the
 * timer and configures GPIO/NVIC. Stop before releasing or stopping the timer.
 * Events and context remain valid through Stop. Callback runs in IRQ context;
 * overcapture indicates at least one sample was overwritten. Polling Read is
 * allowed only without a callback and returns BUSY when no sample is pending. */
ABoxHalStatus ABoxCapture_Configure(unsigned instance, unsigned channel, const ABoxCaptureConfig *config);
ABoxHalStatus ABoxCapture_Start(unsigned instance, unsigned channel, const ABoxCaptureEvents *events);
ABoxHalStatus ABoxCapture_Read(unsigned instance, unsigned channel, uint32_t *counter, uint8_t *overcapture);
ABoxHalStatus ABoxCapture_Stop(unsigned instance, unsigned channel);
ABoxHalStatus ABoxCapture_Release(unsigned instance, unsigned channel);

typedef enum { ABOX_CAPTURE_DMA_HALF, ABOX_CAPTURE_DMA_COMPLETE,
               ABOX_CAPTURE_DMA_ERROR, ABOX_CAPTURE_DMA_OVERCAPTURE } ABoxCaptureDmaEvent;
typedef struct {
    void *context;
    void (*on_event)(void *context,unsigned instance,unsigned channel,ABoxCaptureDmaEvent event);
} ABoxCaptureDmaEvents;
/* Stream 16-bit capture register values into caller storage. Configure the
 * capture first and start the timer separately; the board configures GPIO and
 * dispatches the fixed DMA IRQ. Unsupported physical request routes return
 * UNSUPPORTED and the DMA allocator rejects conflicting users. Callback is in
 * IRQ context and cannot reenter this capture. Circular mode stays active;
 * normal completion stops capturing but retains the channel until StopDma.
 * Overcapture stops the stream and reports data loss. StopDma releases the DMA
 * and returns any pending/previous overcapture in `lost`; a DMA fault returns
 * ERROR after cleanup. Buffer and event storage remain valid through StopDma. */
ABoxHalStatus ABoxCapture_StartDma(unsigned instance,unsigned channel,uint16_t *samples,
                                   uint16_t count,uint8_t circular,const ABoxCaptureDmaEvents *events);
ABoxHalStatus ABoxCapture_StopDma(unsigned instance,unsigned channel,uint8_t *lost);

typedef enum { ABOX_ENCODER_MODE1, ABOX_ENCODER_MODE2, ABOX_ENCODER_BOTH } ABoxEncoderMode;
typedef struct {
    ABoxEncoderMode mode;
    ABoxCaptureEdge edge_a, edge_b;
    uint8_t filter_a, filter_b; /* hardware digital filter code 0..15 */
} ABoxEncoderConfig;
/* Quadrature uses channels 1 and 2 of timers 1..5. Configure only while the
 * timer is stopped and has no configured PWM/capture channels; configure pins
 * separately. Timer_Start/Stop starts/stops the encoder, and Timer_Read returns
 * the wrapping hardware counter. BOTH counts both phase edges (x4); MODE1/MODE2
 * count one phase (x2). Release before reusing the timer for ordinary PWM. */
ABoxHalStatus ABoxEncoder_Configure(unsigned instance, const ABoxEncoderConfig *config);
ABoxHalStatus ABoxEncoder_Release(unsigned instance);
#endif
