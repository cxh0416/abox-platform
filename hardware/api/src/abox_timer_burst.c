#include "abox_timer.h"
#include "abox_dma.h"
#include "abox_timer_burst_backend.h"
#include <stdint.h>

typedef struct {
    const ABoxTimerBurstEvents *events;
    ABoxDmaEvents dma_events;
    uint16_t count;
    uint8_t owned,dma_open,controller,channel,length,circular,half_aligned,request;
    volatile uint8_t fault;
} TimerBurst;
static TimerBurst bursts[5];

static void dma_event(void *context,unsigned controller,unsigned channel,ABoxDmaEvent event)
{
    TimerBurst *s=(TimerBurst *)context;ABoxTimerBurstEvent result;
    unsigned id=(unsigned)(s-bursts)+1U;(void)controller;(void)channel;
    if (!s->owned || s->fault) return;
    if (event==ABOX_DMA_TRANSFER_ERROR) {
        s->fault=1U;ABoxTimerBurstHw_Request(id,s->request,0);result=ABOX_TIMER_BURST_ERROR;
    } else if (event==ABOX_DMA_COMPLETE) {
        if (!s->circular) ABoxTimerBurstHw_Request(id,s->request,0);
        result=ABOX_TIMER_BURST_COMPLETE;
    } else {
        if (!s->half_aligned) return;
        result=ABOX_TIMER_BURST_HALF;
    }
    if (s->events && s->events->on_event) s->events->on_event(s->events->context,id,result);
}
ABoxHalStatus ABoxTimer_StartBurstDma(unsigned id,uint8_t base,uint8_t length,
                                      ABoxTimerBurstDirection direction,uint16_t *words,uint16_t count,
                                      uint8_t circular,const ABoxTimerBurstEvents *events)
{
    return ABoxTimer_StartBurstDmaOn(id,ABOX_TIMER_BURST_UPDATE,base,length,direction,words,count,circular,events);
}
ABoxHalStatus ABoxTimer_StartBurstDmaOn(unsigned id,ABoxTimerBurstRequest request,uint8_t base,uint8_t length,
                                        ABoxTimerBurstDirection direction,uint16_t *words,uint16_t count,
                                        uint8_t circular,const ABoxTimerBurstEvents *events)
{
    /* Logical DMA channels are one-based for both MCU backends. Zero means
     * this timer event is not wired to a DMA channel on F105. */
    static const uint8_t channel[5][7]={
        {5U,2U,3U,6U,4U,4U,4U}, {2U,5U,7U,1U,7U,0U,0U},
        {3U,6U,0U,2U,3U,6U,0U}, {7U,1U,4U,5U,0U,0U,0U},
        {2U,5U,4U,2U,1U,1U,0U}
    };
    ABoxDmaConfig config={ABOX_DMA_MEMORY_TO_PERIPHERAL,2U,2U,0U,1U,0U,1U};
    TimerBurst *s;ABoxHalStatus status;uintptr_t peripheral;
    if (id<1U || id>7U || !length || length>18U || base>17U || (unsigned)base+length>18U ||
        (unsigned)request>ABOX_TIMER_BURST_COM || (unsigned)direction>ABOX_TIMER_BURST_WRITE || !words || ((uintptr_t)words&1U) || !count ||
        count%length || circular>1U || (circular && count%(2U*length)) || (events && !events->on_event)) return ABOX_HAL_INVALID;
    if (id>5U) return ABOX_HAL_UNSUPPORTED;
    if (!channel[id-1U][request]) return ABOX_HAL_UNSUPPORTED;
    /* Only TIM1 implements RCR and BDTR. */
    if (id!=1U && ((base<=12U && (unsigned)base+length>12U) || (unsigned)base+length>17U)) return ABOX_HAL_UNSUPPORTED;
    s=&bursts[id-1U];if (s->owned) return ABOX_HAL_BUSY;
    status=ABoxTimerBurstHw_Reserve(id);if (status!=ABOX_HAL_OK) return status;
    s->owned=1U;s->dma_open=0U;s->controller=id==5U ? 2U : 1U;s->channel=channel[id-1U][request];s->request=(uint8_t)request;
    s->count=count;s->length=length;s->circular=circular;s->half_aligned=(count%(2U*length))==0U;
    s->fault=0U;s->events=events;s->dma_events.context=s;s->dma_events.on_event=dma_event;
    config.direction=direction==ABOX_TIMER_BURST_WRITE ? ABOX_DMA_MEMORY_TO_PERIPHERAL : ABOX_DMA_PERIPHERAL_TO_MEMORY;
    config.circular=circular;
    status=ABoxDma_Open(s->controller,s->channel,&config);if(status!=ABOX_HAL_OK)goto failed;
    s->dma_open=1U;ABoxTimerBurstHw_Config(id,base,length);peripheral=ABoxTimerBurstHw_Address(id);
    status=ABoxDma_Start(s->controller,s->channel,direction==ABOX_TIMER_BURST_WRITE ? (uintptr_t)words : peripheral,
                         direction==ABOX_TIMER_BURST_WRITE ? peripheral : (uintptr_t)words,count,&s->dma_events);
    if(status!=ABOX_HAL_OK)goto failed;
    ABoxTimerBurstHw_Request(id,s->request,1);return ABOX_HAL_OK;
failed:
    if (!s->dma_open || ABoxDma_Close(s->controller,s->channel)==ABOX_HAL_OK) {
        ABoxTimerBurstHw_Release(id);s->owned=0U;s->dma_open=0U;s->events=0;
    }
    return status;
}
ABoxHalStatus ABoxTimer_StopBurstDma(unsigned id,uint8_t *partial)
{
    TimerBurst *s;ABoxHalStatus status;uint16_t remaining;uint8_t fault;
    if (id<1U || id>5U || !partial) return ABOX_HAL_INVALID;
    s=&bursts[id-1U];if (!s->owned || !s->dma_open) return ABOX_HAL_INVALID;
    ABoxTimerBurstHw_Request(id,s->request,0);fault=s->fault;
    status=ABoxDma_Abort(s->controller,s->channel);if(status!=ABOX_HAL_OK)return status;
    status=ABoxDma_Remaining(s->controller,s->channel,&remaining);if(status!=ABOX_HAL_OK)return status;
    status=ABoxDma_Close(s->controller,s->channel);if(status!=ABOX_HAL_OK)return status;
    *partial=(uint8_t)(fault || remaining>s->count || ((s->count-remaining)%s->length)!=0U);
    ABoxTimerBurstHw_Release(id);s->owned=0U;s->dma_open=0U;s->events=0;
    return fault ? ABOX_HAL_ERROR : ABOX_HAL_OK;
}
