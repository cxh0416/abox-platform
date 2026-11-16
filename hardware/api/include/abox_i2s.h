#ifndef ABOX_I2S_H
#define ABOX_I2S_H
#include <stdint.h>
#include "abox_hal_status.h"

/* Physical I2S2/I2S3 share the SPI2/SPI3 controller and pins. */
typedef enum { ABOX_I2S_SLAVE_TX, ABOX_I2S_SLAVE_RX,
               ABOX_I2S_MASTER_TX, ABOX_I2S_MASTER_RX } ABoxI2sMode;
typedef enum { ABOX_I2S_PHILIPS, ABOX_I2S_MSB, ABOX_I2S_LSB,
               ABOX_I2S_PCM_SHORT, ABOX_I2S_PCM_LONG } ABoxI2sStandard;
typedef enum { ABOX_I2S_16_IN_16, ABOX_I2S_16_IN_32,
               ABOX_I2S_24_IN_32, ABOX_I2S_32_IN_32 } ABoxI2sFormat;
typedef struct {
    ABoxI2sMode mode;
    ABoxI2sStandard standard;
    ABoxI2sFormat format;
    uint8_t clock_polarity, master_clock_output;
    /* Master: actual selected I2S kernel-clock frequency and desired stereo
     * frame rate. The board configures and owns the clock source and PLL;
     * this API does not change them. Slave: both values and MCLK are zero. */
    uint32_t clock_hz, sample_hz, max_error_ppm;
} ABoxI2sConfig;
typedef enum { ABOX_I2S_HALF, ABOX_I2S_COMPLETE, ABOX_I2S_DMA_ERROR,
               ABOX_I2S_UNDERRUN, ABOX_I2S_OVERRUN } ABoxI2sEvent;
typedef struct {
    void *context;
    void (*on_event)(void *context,unsigned instance,ABoxI2sEvent event);
} ABoxI2sEvents;

typedef struct {
    uint32_t external_hz; /* board HSE/HXTAL frequency */
    uint8_t divider;      /* shared external predivider, 1..16 */
    uint8_t multiplier;   /* STM32: 8..14, 16, 20; GD32: 8..16, 20 */
    uint8_t i2s2,i2s3;    /* select PLL for each controller, otherwise SYSCLK */
} ABoxI2sPllConfig;
/* Own the I2S PLL and clock selectors while configured. HSE/HXTAL must already
 * be stable, both I2S instances closed, and no other user may change the
 * clock tree. The divider is shared with another PLL: a different divider is
 * refused while that PLL is enabled. Requires an advancing platform tick.
 * Configure reports the actual PLL-derived I2S kernel clock; pass it to Open.
 * Stop all I2S users before Release. The shared divider is left in place. A
 * timeout leaves the PLL disabled and requires board-level recovery if it
 * does not stop. This API does not validate external audio hardware. */
ABoxHalStatus ABoxI2s_ConfigurePll(const ABoxI2sPllConfig *config,uint32_t timeout_ms,uint32_t *kernel_hz);
ABoxHalStatus ABoxI2s_ReleasePll(uint32_t timeout_ms);

/* Open reserves the shared SPI instance, calculates a hardware divider and
 * returns the integer actual frame rate for masters (zero for slaves). GPIO,
 * kernel clock selection and external audio wiring remain board duties. The
 * caller serializes the instance and does not reconfigure its clock while open.
 * STM32F105 returns UNSUPPORTED for master PCM long with 16-bit data extended
 * to 32-bit slots because of the documented WS timing erratum. In slave mode
 * the external master must supply the required WS level at stream start and
 * the application must handle synchronization loss due to noisy clock/WS. */
ABoxHalStatus ABoxI2s_Open(unsigned instance,const ABoxI2sConfig *config,uint32_t *actual_hz);
ABoxHalStatus ABoxI2s_Close(unsigned instance);
/* Polling streams transfer one 16-bit hardware data register word per call.
 * Start preloads first_tx for TX; pass NULL for RX. PollRead/PollWrite return
 * BUSY if no word can be transferred yet and ERROR on overrun/underrun. The
 * caller handles sample packing and services the stream fast enough to avoid
 * loss. Stop immediately disables I2S and may truncate a frame. On error,
 * Stop and Close/Open before reuse. This mode and DMA cannot run together. */
ABoxHalStatus ABoxI2s_StartPolling(unsigned instance,const uint16_t *first_tx);
ABoxHalStatus ABoxI2s_PollWrite(unsigned instance,uint16_t word);
ABoxHalStatus ABoxI2s_PollRead(unsigned instance,uint16_t *word);
ABoxHalStatus ABoxI2s_StopPolling(unsigned instance);
typedef struct {
    void *context;
    uint16_t (*next_tx)(void *context,unsigned instance);
    void (*received)(void *context,unsigned instance,uint16_t word);
    void (*error)(void *context,unsigned instance,ABoxI2sEvent event);
} ABoxI2sIrqEvents;
/* Interrupt streams use the same halfword packing as polling. Start preloads
 * first_tx for TX; RX passes NULL. Supply next_tx or received for the selected
 * direction. The board enables the SPI2/SPI3 IRQ and dispatches ABoxI2s_Irq.
 * Callbacks execute in IRQ context and cannot reenter this instance; events
 * and context remain valid through StopIrq. Stop may truncate a frame. If an
 * error is pending when stopping, StopIrq reports ERROR even if the error IRQ
 * was not dispatched. Stop and Close/Open after an error. */
ABoxHalStatus ABoxI2s_StartIrq(unsigned instance,const uint16_t *first_tx,const ABoxI2sIrqEvents *events);
ABoxHalStatus ABoxI2s_StopIrq(unsigned instance);
/* DMA storage is 16-bit aligned halfwords. A stereo frame uses two halfwords
 * for 16-bit data and four for 24/32-bit data; 24-bit samples occupy two
 * halfwords, whose significant-bit positions depend on the selected standard.
 * PCM uses the same two-sample buffer grouping without left/right semantics.
 * Count must contain whole groups, and circular buffers must have
 * an even number of frames so half interrupts do not split a frame. For a
 * non-circular buffer with an odd number of frames, the half event is omitted.
 * Supply only tx_words for TX mode or only rx_words for RX mode, as selected
 * when opening. Board code dispatches the fixed DMA IRQ and
 * SPI2/SPI3 error IRQ to ABoxI2s_Irq. Callbacks execute in IRQ context and must
 * not reenter this instance. Buffer and event storage remain valid through Stop.
 * DMA completion means memory was transferred, not that final TX bits left the
 * pin. Stop immediately disables I2S and may truncate the last frame; continuous
 * audio normally uses circular DMA. Underrun, overrun, DMA error or Abort require
 * Close/Open. No callback or return value proves peer playback or capture. */
ABoxHalStatus ABoxI2s_StartDma(unsigned instance,const uint16_t *tx_words,uint16_t *rx_words,
                               uint16_t count,uint8_t circular,
                               const ABoxI2sEvents *events);
ABoxHalStatus ABoxI2s_StopDma(unsigned instance);
ABoxHalStatus ABoxI2s_AbortDma(unsigned instance);
void ABoxI2s_Irq(unsigned instance);
#endif
