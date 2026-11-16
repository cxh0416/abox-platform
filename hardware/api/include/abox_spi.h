#ifndef ABOX_SPI_H
#define ABOX_SPI_H

#include <stdint.h>
#include "abox_hal_status.h"

/* Instance 1..3 denotes the F105 SPI1..SPI3 peripheral. Pins, remapping,
 * chip select and external bus ownership are configured separately. */
typedef enum { ABOX_SPI_MASTER, ABOX_SPI_SLAVE } ABoxSpiRole;
typedef enum { ABOX_SPI_NSS_SOFTWARE, ABOX_SPI_NSS_INPUT,
               ABOX_SPI_NSS_OUTPUT } ABoxSpiNss;
typedef enum { ABOX_SPI_TWO_LINE_FULL, ABOX_SPI_TWO_LINE_RX,
               ABOX_SPI_ONE_LINE } ABoxSpiWiring;
typedef struct {
    ABoxSpiRole role;
    ABoxSpiNss nss;
    uint16_t prescaler; /* 2, 4, 8, 16, 32, 64, 128 or 256 */
    uint8_t bits;       /* 8 or 16 */
    uint8_t cpol;       /* 0 or 1 */
    uint8_t cpha;       /* 0 or 1 */
    uint8_t lsb_first;  /* 0 or 1 */
    ABoxSpiWiring wiring;
} ABoxSpiConfig;

/* One owner must serialize configuration and transfers for each instance.
 * A working platform millisecond tick is required for bounded transfers;
 * do not use the blocking API from an IRQ that prevents the tick advancing.
 * The caller supplies GPIO configuration and guards shared external devices.
 * Open does not configure pins or start a transfer; hardware NSS can drive
 * the configured pin when the controller is enabled for a transfer. */
ABoxHalStatus ABoxSpi_Open(unsigned instance, const ABoxSpiConfig *config);
ABoxHalStatus ABoxSpi_Close(unsigned instance);
/* Transfer `frames` 8- or 16-bit frames. In full-duplex mode a null tx sends
 * ones and a null rx discards data. In two-line RX mode only rx is allowed;
 * in one-line mode exactly one of tx or rx is required per transfer.
 * Buffers may be identical for in-place transfer, but must not partially
 * overlap. For 16-bit frames, non-null buffers must be 2-byte aligned. A failure may
 * leave a partial transaction on the wire; Close/Open resets the controller
 * before another transfer. Master receive-only modes generate clocks while
 * enabled; stopping at an exact frame boundary depends on interrupt/bus timing
 * and must be checked against the attached peripheral. Success does not prove
 * peer acceptance. */
ABoxHalStatus ABoxSpi_Transfer(unsigned instance, const void *tx, void *rx,
                               uint16_t frames, uint32_t timeout_ms);
/* Blocking transfer with the controller's CRC8 or CRC16, chosen
 * by the configured frame width. Both peers must use the same polynomial,
 * framing, bit order and CRC protocol. The polynomial must be odd and fit the
 * selected frame width. The controller appends one CRC frame and verifies the
 * received CRC in receive modes; it is not included in either buffer or frames.
 * In one-line transmit mode there is no received CRC to check. The caller
 * controls chip select and must choose a clock/interrupt latency that allows
 * CRCNEXT to be set before the final frame finishes. In receive-only master
 * mode the controller can generate extra clocks before software stops it,
 * especially when receiving the CRC. An error invalidates the
 * received payload and requires Close/Open; buffers may be partially changed.
 * On F105 the CRC can be corrupted by another peripheral using either mapped
 * DMA channel during a CPU transfer. This call reserves both channels without
 * starting DMA; the board must also exclude direct vendor-library DMA users.
 * When sharing SCK with other devices in slave mode, both peers reset CRC while
 * this slave is deselected and avoid clock edges until it is selected again. */
ABoxHalStatus ABoxSpi_TransferCrc(unsigned instance,const void *tx,void *rx,uint16_t frames,
                                  uint16_t polynomial,uint32_t timeout_ms);

typedef struct {
    void *context;
    /* All DMA buffers are transferred, or a DMA error occurred. Callback
     * runs in IRQ context; it must not reenter this SPI instance. */
    void (*buffers_ready)(void *context, unsigned instance, ABoxHalStatus status);
} ABoxSpiDmaEvents;
/* Full-duplex mode reserves both fixed DMA channels and arms RX before TX; a
 * null tx transmits all ones and a null rx discards frames. Two-line RX and
 * one-line transfers reserve only the channel for the selected direction.
 * The receive-only peripheral is disabled in its DMA completion IRQ: an
 * externally clocked master must stop at the requested length, and an F105
 * master may emit extra clocks before that IRQ runs. Verify that this timing
 * is acceptable to the peer; use full-duplex with dummy TX for bounded clocks.
 * Storage and events
 * remain valid through FinishDma/AbortDma. The board controls chip select and
 * dispatches both DMA IRQs. A buffers_ready callback does not mean that the
 * final clock edge has completed: the caller must FinishDma outside the IRQ,
 * which waits for the controller to become idle and releases its channels.
 * Finish returns BUSY before the callback; after timeout, DMA error or Abort,
 * Close/Open is required before another transaction. Abort may leave a partial
 * transaction on the wire. Other operations on this SPI instance return BUSY
 * while DMA is reserved. */
ABoxHalStatus ABoxSpi_StartDma(unsigned instance, const void *tx, void *rx,
                                uint16_t frames, const ABoxSpiDmaEvents *events);
/* DMA with hardware CRC. The controller automatically appends or receives
 * one CRC frame after the DMA payload; do not include it in frames
 * or buffers. buffers_ready reports only the payload DMA, while FinishDma
 * waits for the CRC in receive modes and bus idle and reports a mismatch.
 * One-line transmit has no received CRC to check. For receive-only master
 * mode the bus may clock additional frames before FinishDma runs; the caller
 * must ensure that this is acceptable and call FinishDma promptly. The caller keeps the
 * peer and chip select active until FinishDma returns. The polynomial must be
 * odd and fit the configured 8/16-bit frame width. Circular DMA is not used. */
ABoxHalStatus ABoxSpi_StartDmaCrc(unsigned instance,const void *tx,void *rx,uint16_t frames,
                                  uint16_t polynomial,const ABoxSpiDmaEvents *events);
ABoxHalStatus ABoxSpi_FinishDma(unsigned instance, uint32_t timeout_ms);
ABoxHalStatus ABoxSpi_AbortDma(unsigned instance);
#endif
