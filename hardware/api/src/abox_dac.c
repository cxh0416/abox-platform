#include "abox_dac_backend.h"
typedef struct {
    uint8_t opened, enabled, dma_owned, dma_circular;
    ABoxDacTrigger trigger;
    const ABoxDacDmaEvents *events;
    ABoxDmaEvents dma_events;
} DacSlot;
static DacSlot slots[2];
static DacSlot *get(unsigned channel) { return channel >= 1U && channel <= 2U ? &slots[channel-1U] : 0; }
ABoxHalStatus ABoxDac_Open(unsigned channel, const ABoxDacConfig *config)
{
    DacSlot *s=get(channel); ABoxHalStatus result;
    if (!s || !config || (unsigned)config->trigger > ABOX_DAC_EXTI9 || config->output_buffer > 1U ||
        config->initial_value > 4095U || (unsigned)config->wave>ABOX_DAC_WAVE_TRIANGLE ||
        (config->wave==ABOX_DAC_WAVE_NONE && config->wave_bits!=0U) ||
        (config->wave!=ABOX_DAC_WAVE_NONE &&
         (!config->wave_bits || config->wave_bits>12U || config->trigger==ABOX_DAC_IMMEDIATE))) return ABOX_HAL_INVALID;
    if (s->opened) return ABOX_HAL_BUSY;
    result=ABoxDacHw_Open(channel,config);
    if (result != ABOX_HAL_OK) return result;
    s->opened=1U; s->enabled=0U; s->trigger=config->trigger;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDac_Enable(unsigned channel)
{
    DacSlot *s=get(channel); if (!s || !s->opened) return ABOX_HAL_INVALID;
    if (s->enabled) return ABOX_HAL_BUSY;
    ABoxDacHw_Enable(channel); s->enabled=1U; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDac_Disable(unsigned channel)
{
    DacSlot *s=get(channel); if (!s || !s->opened) return ABOX_HAL_INVALID;
    if (s->enabled) ABoxDacHw_Disable(channel);
    s->enabled=0U;
    return s->dma_owned ? ABoxDac_StopDma(channel) : ABOX_HAL_OK;
}
ABoxHalStatus ABoxDac_Close(unsigned channel)
{
    DacSlot *s=get(channel); if (!s || !s->opened) return ABOX_HAL_INVALID;
    ABoxHalStatus status=ABoxDac_Disable(channel);
    if (status!=ABOX_HAL_OK) return status;
    s->opened=0U; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDac_Write(unsigned channel, uint16_t value)
{
    DacSlot *s=get(channel); if (!s || !s->opened || value>4095U) return ABOX_HAL_INVALID;
    if (s->dma_owned) return ABOX_HAL_BUSY;
    ABoxDacHw_Write(channel,value); return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDac_Trigger(unsigned channel)
{
    DacSlot *s=get(channel); if (!s || !s->opened || !s->enabled || s->trigger!=ABOX_DAC_SOFTWARE) return ABOX_HAL_INVALID;
    ABoxDacHw_Trigger(channel); return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDac_ReadOutput(unsigned channel, uint16_t *value)
{
    DacSlot *s=get(channel); if (!s || !s->opened || !value) return ABOX_HAL_INVALID;
    *value=ABoxDacHw_ReadOutput(channel); return ABOX_HAL_OK;
}

static void dma_event(void *context, unsigned controller, unsigned dma_channel, ABoxDmaEvent event)
{
    DacSlot *s=(DacSlot *)context;
    unsigned channel=(unsigned)(s-slots)+1U;
    const ABoxDacDmaEvents *events=s->events;
    (void)controller; (void)dma_channel;
    if (event==ABOX_DMA_TRANSFER_ERROR || (event==ABOX_DMA_COMPLETE && !s->dma_circular)) {
        ABoxDacHw_DmaRequest(channel,0);
    }
    if (events && events->on_event) events->on_event(events->context,channel,event);
}
ABoxHalStatus ABoxDac_StartDma(unsigned channel,const uint16_t *samples,uint16_t count,
                               uint8_t circular,const ABoxDacDmaEvents *events)
{
    DacSlot *s=get(channel); ABoxHalStatus status;
    ABoxDmaConfig config={ABOX_DMA_MEMORY_TO_PERIPHERAL,2U,2U,0U,1U,0U,1U};
    if (!s || !s->opened || !samples || ((uintptr_t)samples&1U) || !count || circular>1U || s->trigger==ABOX_DAC_IMMEDIATE)
        return ABOX_HAL_INVALID;
    if (s->dma_owned) return ABOX_HAL_BUSY;
    for (uint16_t i=0U;i<count;++i) if (samples[i]>4095U) return ABOX_HAL_INVALID;
    config.circular=circular;
    status=ABoxDma_Open(2U,channel+2U,&config);
    if (status!=ABOX_HAL_OK) return status;
    s->dma_owned=1U; s->dma_circular=circular; s->events=events;
    s->dma_events.context=s; s->dma_events.on_event=dma_event;
    status=ABoxDma_Start(2U,channel+2U,(uintptr_t)samples,ABoxDacHw_DmaAddress(channel),count,&s->dma_events);
    if (status!=ABOX_HAL_OK) {
        if (ABoxDma_Close(2U,channel+2U)==ABOX_HAL_OK) { s->dma_owned=0U; s->events=0; }
        return status;
    }
    ABoxDacHw_DmaRequest(channel,1);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDac_StopDma(unsigned channel)
{
    DacSlot *s=get(channel); ABoxHalStatus status;
    if (!s || !s->opened) return ABOX_HAL_INVALID;
    if (!s->dma_owned) return ABOX_HAL_OK;
    ABoxDacHw_DmaRequest(channel,0);
    status=ABoxDma_Abort(2U,channel+2U);
    if (status!=ABOX_HAL_OK) return status;
    status=ABoxDma_Close(2U,channel+2U);
    if (status!=ABOX_HAL_OK) return status;
    s->dma_owned=0U; s->dma_circular=0U; s->events=0; return ABOX_HAL_OK;
}
