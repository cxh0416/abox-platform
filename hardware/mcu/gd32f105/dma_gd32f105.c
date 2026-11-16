#include <string.h>
#include "abox_dma_common.h"
#include "abox_dma_resource.h"
#include "gd32f10x.h"

typedef struct {
    ABoxDmaConfig config;
    const ABoxDmaEvents *events;
    volatile uint8_t open, active, fault;
} DmaInstance;
static DmaInstance g_dma[2][7];
static uint32_t dma(unsigned controller) { return controller == 1U ? DMA0 : DMA1; }
static dma_channel_enum ch(unsigned channel) { return (dma_channel_enum)(channel - 1U); }
static void emit(DmaInstance *s, unsigned controller, unsigned channel, ABoxDmaEvent event)
{
    const ABoxDmaEvents *e = s->events;
    if (e && e->on_event) e->on_event(e->context, controller, channel, event);
}
static void stop(unsigned controller, unsigned channel)
{
    uint32_t instance = dma(controller);
    dma_interrupt_disable(instance, ch(channel), DMA_INT_FTF | DMA_INT_HTF | DMA_INT_ERR);
    dma_channel_disable(instance, ch(channel));
    dma_flag_clear(instance, ch(channel), DMA_FLAG_G);
}

ABoxHalStatus ABoxDma_Open(unsigned controller, unsigned channel, const ABoxDmaConfig *config)
{
    DmaInstance *s;
    if (!ABoxDma_ValidId(controller, channel) || !ABoxDma_ConfigValid(config)) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (s->open || !ABoxDmaResource_Claim(controller, channel)) return ABOX_HAL_BUSY;
    rcu_periph_clock_enable(controller == 1U ? RCU_DMA0 : RCU_DMA1);
    dma_deinit(dma(controller), ch(channel));
    s->config = *config; s->events = 0; s->fault = 0U; s->active = 0U; s->open = 1U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Start(unsigned controller, unsigned channel, uintptr_t source,
                            uintptr_t destination, uint16_t count, const ABoxDmaEvents *events)
{
    DmaInstance *s;
    dma_parameter_struct p;
    uint32_t instance;
    if (!ABoxDma_ValidId(controller, channel)) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open || !ABoxDma_TransferValid(&s->config, source, destination, count)) return ABOX_HAL_INVALID;
    if (s->active) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    instance = dma(controller);
    dma_deinit(instance, ch(channel));
    dma_struct_para_init(&p);
    p.periph_addr = (uint32_t)(s->config.direction == ABOX_DMA_MEMORY_TO_PERIPHERAL ? destination : source);
    p.memory_addr = (uint32_t)(s->config.direction == ABOX_DMA_MEMORY_TO_PERIPHERAL ? source : destination);
    p.direction = s->config.direction == ABOX_DMA_MEMORY_TO_PERIPHERAL ? DMA_MEMORY_TO_PERIPHERAL : DMA_PERIPHERAL_TO_MEMORY;
    p.periph_inc = s->config.peripheral_increment ? DMA_PERIPH_INCREASE_ENABLE : DMA_PERIPH_INCREASE_DISABLE;
    p.memory_inc = s->config.memory_increment ? DMA_MEMORY_INCREASE_ENABLE : DMA_MEMORY_INCREASE_DISABLE;
    p.periph_width = s->config.peripheral_width == 1U ? DMA_PERIPHERAL_WIDTH_8BIT :
        s->config.peripheral_width == 2U ? DMA_PERIPHERAL_WIDTH_16BIT : DMA_PERIPHERAL_WIDTH_32BIT;
    p.memory_width = s->config.memory_width == 1U ? DMA_MEMORY_WIDTH_8BIT :
        s->config.memory_width == 2U ? DMA_MEMORY_WIDTH_16BIT : DMA_MEMORY_WIDTH_32BIT;
    p.priority = (uint32_t)s->config.priority << 12U;
    p.number = count;
    dma_init(instance, ch(channel), &p);
    if (s->config.circular) dma_circulation_enable(instance, ch(channel));
    if (s->config.direction == ABOX_DMA_MEMORY_TO_MEMORY) dma_memory_to_memory_enable(instance, ch(channel));
    dma_flag_clear(instance, ch(channel), DMA_FLAG_G);
    s->events = events; s->active = 1U;
    dma_interrupt_enable(instance, ch(channel), DMA_INT_FTF | DMA_INT_HTF | DMA_INT_ERR);
    dma_channel_enable(instance, ch(channel));
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Abort(unsigned controller, unsigned channel)
{
    DmaInstance *s;
    if (!ABoxDma_ValidId(controller, channel)) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    stop(controller, channel);
    s->active = 0U; s->fault = 0U; s->events = 0;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Close(unsigned controller, unsigned channel)
{
    DmaInstance *s;
    if (!ABoxDma_ValidId(controller, channel)) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->active) return ABOX_HAL_BUSY;
    stop(controller, channel);
    dma_deinit(dma(controller), ch(channel));
    s->open = 0U; s->fault = 0U; s->events = 0;
    ABoxDmaResource_Release(controller, channel);
    /* DMA controller clocks are shared with other channels. */
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Remaining(unsigned controller, unsigned channel, uint16_t *count)
{
    DmaInstance *s;
    if (!ABoxDma_ValidId(controller, channel) || !count) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    *count = (uint16_t)dma_transfer_number_get(dma(controller), ch(channel));
    return ABOX_HAL_OK;
}
void ABoxDma_Irq(unsigned controller, unsigned channel)
{
    DmaInstance *s;
    uint32_t instance;
    if (!ABoxDma_ValidId(controller, channel)) return;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open || !s->active) return;
    instance = dma(controller);
    if (dma_interrupt_flag_get(instance, ch(channel), DMA_INT_FLAG_ERR) != RESET) {
        stop(controller, channel);
        s->active = 0U; s->fault = 1U;
        emit(s, controller, channel, ABOX_DMA_TRANSFER_ERROR);
    } else if (dma_interrupt_flag_get(instance, ch(channel), DMA_INT_FLAG_HTF) != RESET) {
        dma_interrupt_flag_clear(instance, ch(channel), DMA_INT_FLAG_HTF);
        if (!s->config.circular) dma_interrupt_disable(instance, ch(channel), DMA_INT_HTF);
        emit(s, controller, channel, ABOX_DMA_HALF);
    } else if (dma_interrupt_flag_get(instance, ch(channel), DMA_INT_FLAG_FTF) != RESET) {
        dma_interrupt_flag_clear(instance, ch(channel), DMA_INT_FLAG_FTF);
        if (!s->config.circular) {
            dma_interrupt_disable(instance, ch(channel), DMA_INT_FTF | DMA_INT_HTF | DMA_INT_ERR);
            s->active = 0U;
        }
        emit(s, controller, channel, ABOX_DMA_COMPLETE);
    }
}
