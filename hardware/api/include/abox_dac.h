#ifndef ABOX_DAC_H
#define ABOX_DAC_H
#include <stdint.h>
#include "abox_hal_status.h"
#include "abox_dma.h"

typedef enum { ABOX_DAC_IMMEDIATE, ABOX_DAC_SOFTWARE, ABOX_DAC_TIM2, ABOX_DAC_TIM3,
               ABOX_DAC_TIM4, ABOX_DAC_TIM5, ABOX_DAC_TIM6, ABOX_DAC_TIM7,
               ABOX_DAC_EXTI9 } ABoxDacTrigger;
typedef enum { ABOX_DAC_WAVE_NONE, ABOX_DAC_WAVE_NOISE, ABOX_DAC_WAVE_TRIANGLE } ABoxDacWave;
typedef struct {
    ABoxDacTrigger trigger;
    uint8_t output_buffer;
    uint16_t initial_value;
    ABoxDacWave wave;
    /* NONE: zero. NOISE: unmasked low LFSR bits, 1..12. TRIANGLE: amplitude
     * (2^wave_bits - 1), with wave_bits 1..12. Wave modes require a trigger. */
    uint8_t wave_bits;
} ABoxDacConfig;
/* Physical DAC channels 1 and 2; reserve the shared DAC block, GPIO and
 * trigger source against other users. Calls on either channel must be
 * serialized. Open configures the holding register but does not enable the
 * analog output. The board owns pin mode and safe external load behavior. */
ABoxHalStatus ABoxDac_Open(unsigned channel, const ABoxDacConfig *config);
ABoxHalStatus ABoxDac_Enable(unsigned channel);
ABoxHalStatus ABoxDac_Disable(unsigned channel);
ABoxHalStatus ABoxDac_Close(unsigned channel);
/* Right-aligned 12-bit code. Immediate mode converts on Write; hardware and
 * software trigger modes wait for their selected trigger. Trigger is valid
 * only for an enabled software-triggered channel. The caller configures the
 * timer TRGO or EXTI event. A configured noise or triangle signal is added to
 * the holding value on triggers; the caller chooses offset and amplitude for
 * its output range. A register value is not a measured voltage. */
ABoxHalStatus ABoxDac_Write(unsigned channel, uint16_t value);
ABoxHalStatus ABoxDac_Trigger(unsigned channel);
ABoxHalStatus ABoxDac_ReadOutput(unsigned channel, uint16_t *value);

typedef struct {
    void *context;
    void (*on_event)(void *context, unsigned channel, ABoxDmaEvent event);
} ABoxDacDmaEvents;
/* DMA writes right-aligned 12-bit samples on the selected DAC trigger.
 * Configure and hold the trigger source stopped until the channel, DMA and
 * output are ready. StartDma reserves DMA2 channel 3 or 4; callers configure
 * and dispatch the corresponding NVIC IRQ through ABoxDma_Irq. Normal
 * completion disables requests but keeps the channel reserved until StopDma.
 * Circular completion keeps running. Events/context/buffer remain valid until
 * StopDma; callbacks run in IRQ context and must not reenter this channel.
 * StopDma halts further DMA requests, not a trigger already being processed.
 * The initial output and first triggered sample are controlled by the DAC's
 * configured holding register and hardware pipeline; wave generation also
 * applies to DMA-provided holding values. DMA completion does not
 * prove a measured analog voltage. */
ABoxHalStatus ABoxDac_StartDma(unsigned channel, const uint16_t *samples,
                               uint16_t count, uint8_t circular, const ABoxDacDmaEvents *events);
ABoxHalStatus ABoxDac_StopDma(unsigned channel);
#endif
