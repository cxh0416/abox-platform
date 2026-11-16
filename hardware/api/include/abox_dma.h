#ifndef ABOX_DMA_H
#define ABOX_DMA_H

#include <stdint.h>
#include "abox_hal_status.h"

/* Controller 1 has channels 1..7; controller 2 has channels 1..5.
 * Channel request mapping is fixed by the MCU. ABox DMA and UART reserve
 * their channels jointly. The board must exclude external vendor-library users. */
typedef enum { ABOX_DMA_PERIPHERAL_TO_MEMORY, ABOX_DMA_MEMORY_TO_PERIPHERAL,
               ABOX_DMA_MEMORY_TO_MEMORY } ABoxDmaDirection;
typedef struct {
    ABoxDmaDirection direction;
    uint8_t peripheral_width, memory_width; /* bytes: 1, 2 or 4 */
    uint8_t peripheral_increment, memory_increment;
    uint8_t circular;
    uint8_t priority; /* 0..3, lowest to highest */
} ABoxDmaConfig;
typedef enum { ABOX_DMA_HALF, ABOX_DMA_COMPLETE, ABOX_DMA_TRANSFER_ERROR } ABoxDmaEvent;
typedef struct {
    void *context;
    void (*on_event)(void *context, unsigned controller, unsigned channel, ABoxDmaEvent event);
} ABoxDmaEvents;

/* Caller serializes the channel, including IRQ dispatch, and configures NVIC.
 * For shared IRQ lines dispatch each owned channel. The event table/context
 * must remain valid until completion, abort or close; callbacks run in IRQ
 * context and must not reenter the same channel. Circular transfers remain
 * active across completion events.
 * Configure the peripheral DMA request separately. Neither completion nor
 * the memory contents alone proves that the peripheral transaction finished. */
ABoxHalStatus ABoxDma_Open(unsigned controller, unsigned channel, const ABoxDmaConfig *config);
ABoxHalStatus ABoxDma_Start(unsigned controller, unsigned channel, uintptr_t source,
                            uintptr_t destination, uint16_t count, const ABoxDmaEvents *events);
ABoxHalStatus ABoxDma_Abort(unsigned controller, unsigned channel);
ABoxHalStatus ABoxDma_Close(unsigned controller, unsigned channel);
ABoxHalStatus ABoxDma_Remaining(unsigned controller, unsigned channel, uint16_t *count);
void ABoxDma_Irq(unsigned controller, unsigned channel);

#endif
