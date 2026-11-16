#include <string.h>
#include "abox_dma_common.h"
#include "abox_dma_resource.h"
#include "stm32f1xx_hal.h"

typedef struct {
    DMA_HandleTypeDef handle;
    ABoxDmaConfig config;
    const ABoxDmaEvents *events;
    unsigned controller, channel;
    volatile uint8_t open, active, fault;
} DmaInstance;
static DmaInstance g_dma[2][7];

static DMA_Channel_TypeDef *channel_address(unsigned controller, unsigned channel)
{
    static DMA_Channel_TypeDef *const channels[2][7] = {
        {DMA1_Channel1, DMA1_Channel2, DMA1_Channel3, DMA1_Channel4, DMA1_Channel5, DMA1_Channel6, DMA1_Channel7},
        {DMA2_Channel1, DMA2_Channel2, DMA2_Channel3, DMA2_Channel4, DMA2_Channel5, 0, 0}
    };
    return ABoxDma_ValidId(controller, channel) ? channels[controller - 1U][channel - 1U] : 0;
}
static void emit(DmaInstance *s, ABoxDmaEvent event)
{
    const ABoxDmaEvents *e = s->events;
    if (e && e->on_event) e->on_event(e->context, s->controller, s->channel, event);
}
static void half(DMA_HandleTypeDef *h) { emit((DmaInstance *)h->Parent, ABOX_DMA_HALF); }
static void complete(DMA_HandleTypeDef *h)
{
    DmaInstance *s = (DmaInstance *)h->Parent;
    if (!s->config.circular) s->active = 0U;
    emit(s, ABOX_DMA_COMPLETE);
}
static void error(DMA_HandleTypeDef *h)
{
    DmaInstance *s = (DmaInstance *)h->Parent;
    s->active = 0U; s->fault = 1U;
    emit(s, ABOX_DMA_TRANSFER_ERROR);
}

ABoxHalStatus ABoxDma_Open(unsigned controller, unsigned channel, const ABoxDmaConfig *config)
{
    DmaInstance *s;
    DMA_Channel_TypeDef *address = channel_address(controller, channel);
    if (!address || !ABoxDma_ConfigValid(config)) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (s->open || !ABoxDmaResource_Claim(controller, channel)) return ABOX_HAL_BUSY;
    memset(s, 0, sizeof(*s));
    s->handle.Instance = address;
    s->handle.Init.Direction = config->direction == ABOX_DMA_PERIPHERAL_TO_MEMORY ? DMA_PERIPH_TO_MEMORY :
                               config->direction == ABOX_DMA_MEMORY_TO_PERIPHERAL ? DMA_MEMORY_TO_PERIPH : DMA_MEMORY_TO_MEMORY;
    s->handle.Init.PeriphInc = config->peripheral_increment ? DMA_PINC_ENABLE : DMA_PINC_DISABLE;
    s->handle.Init.MemInc = config->memory_increment ? DMA_MINC_ENABLE : DMA_MINC_DISABLE;
    s->handle.Init.PeriphDataAlignment = config->peripheral_width == 1U ? DMA_PDATAALIGN_BYTE :
        config->peripheral_width == 2U ? DMA_PDATAALIGN_HALFWORD : DMA_PDATAALIGN_WORD;
    s->handle.Init.MemDataAlignment = config->memory_width == 1U ? DMA_MDATAALIGN_BYTE :
        config->memory_width == 2U ? DMA_MDATAALIGN_HALFWORD : DMA_MDATAALIGN_WORD;
    s->handle.Init.Mode = config->circular ? DMA_CIRCULAR : DMA_NORMAL;
    s->handle.Init.Priority = (uint32_t)config->priority << DMA_CCR_PL_Pos;
    if (controller == 1U) __HAL_RCC_DMA1_CLK_ENABLE(); else __HAL_RCC_DMA2_CLK_ENABLE();
    if (HAL_DMA_Init(&s->handle) != HAL_OK) {
        ABoxDmaResource_Release(controller, channel);
        return ABOX_HAL_ERROR;
    }
    s->config = *config; s->controller = controller; s->channel = channel;
    s->handle.Parent = s;
    s->handle.XferHalfCpltCallback = half; s->handle.XferCpltCallback = complete;
    s->handle.XferErrorCallback = error; s->open = 1U;
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxDma_Start(unsigned controller, unsigned channel, uintptr_t source,
                            uintptr_t destination, uint16_t count, const ABoxDmaEvents *events)
{
    DmaInstance *s;
    HAL_StatusTypeDef result;
    if (!ABoxDma_ValidId(controller, channel)) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open || !ABoxDma_TransferValid(&s->config, source, destination, count)) return ABOX_HAL_INVALID;
    if (s->active) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    s->events = events; s->active = 1U;
    result = HAL_DMA_Start_IT(&s->handle, (uint32_t)source, (uint32_t)destination, count);
    if (result == HAL_OK) return ABOX_HAL_OK;
    s->active = 0U;
    return result == HAL_BUSY ? ABOX_HAL_BUSY : ABOX_HAL_ERROR;
}

ABoxHalStatus ABoxDma_Abort(unsigned controller, unsigned channel)
{
    DmaInstance *s;
    if (!ABoxDma_ValidId(controller, channel)) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->active && HAL_DMA_Abort(&s->handle) != HAL_OK) return ABOX_HAL_ERROR;
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
    if (HAL_DMA_DeInit(&s->handle) != HAL_OK) return ABOX_HAL_ERROR;
    s->open = 0U; s->fault = 0U; s->events = 0;
    ABoxDmaResource_Release(controller, channel);
    /* Do not gate the controller clock: other channels may be in use. */
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Remaining(unsigned controller, unsigned channel, uint16_t *count)
{
    DmaInstance *s;
    if (!ABoxDma_ValidId(controller, channel) || !count) return ABOX_HAL_INVALID;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    *count = (uint16_t)__HAL_DMA_GET_COUNTER(&s->handle);
    return ABOX_HAL_OK;
}
void ABoxDma_Irq(unsigned controller, unsigned channel)
{
    DmaInstance *s;
    if (!ABoxDma_ValidId(controller, channel)) return;
    s = &g_dma[controller - 1U][channel - 1U];
    if (!s->open || !s->active) return;
    /* The pinned ST HAL gives half/complete precedence over transfer error.
     * Report an error first when flags coincide, never false success. */
    if (__HAL_DMA_GET_FLAG(&s->handle, __HAL_DMA_GET_TE_FLAG_INDEX(&s->handle))) {
        (void)HAL_DMA_Abort(&s->handle);
        error(&s->handle);
        return;
    }
    HAL_DMA_IRQHandler(&s->handle);
}
