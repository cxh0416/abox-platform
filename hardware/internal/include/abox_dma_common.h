#ifndef ABOX_DMA_COMMON_H
#define ABOX_DMA_COMMON_H
#include <stdint.h>
#include "abox_dma.h"

static inline int ABoxDma_ValidId(unsigned controller, unsigned channel)
{
    return (controller == 1U && channel >= 1U && channel <= 7U) ||
           (controller == 2U && channel >= 1U && channel <= 5U);
}
static inline int ABoxDma_WidthValid(unsigned width)
{ return width == 1U || width == 2U || width == 4U; }
static inline int ABoxDma_ConfigValid(const ABoxDmaConfig *c)
{
    return c && (unsigned)c->direction <= ABOX_DMA_MEMORY_TO_MEMORY &&
           ABoxDma_WidthValid(c->peripheral_width) && ABoxDma_WidthValid(c->memory_width) &&
           c->peripheral_increment <= 1U && c->memory_increment <= 1U &&
           c->circular <= 1U && c->priority <= 3U &&
           !(c->circular && c->direction == ABOX_DMA_MEMORY_TO_MEMORY);
}
static inline int ABoxDma_RangeValid(uintptr_t address, unsigned width, unsigned increment, uint16_t count)
{
    uint64_t end;
    if (!address || (address & (width - 1U)) != 0U) return 0;
#if UINTPTR_MAX > UINT32_MAX
    if (address > UINT32_MAX) return 0;
#endif
    end = (uint64_t)address + (increment ? (uint64_t)(count - 1U) * width : 0U) + width;
    return end <= 0x100000000ULL;
}
static inline int ABoxDma_TransferValid(const ABoxDmaConfig *c, uintptr_t source,
                                       uintptr_t destination, uint16_t count)
{
    uintptr_t peripheral, memory;
    if (!c || !count) return 0;
    peripheral = c->direction == ABOX_DMA_MEMORY_TO_PERIPHERAL ? destination : source;
    memory = c->direction == ABOX_DMA_MEMORY_TO_PERIPHERAL ? source : destination;
    return ABoxDma_RangeValid(peripheral, c->peripheral_width, c->peripheral_increment, count) &&
           ABoxDma_RangeValid(memory, c->memory_width, c->memory_increment, count);
}
#endif
