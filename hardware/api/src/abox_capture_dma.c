#include "abox_capture_dma_backend.h"
#include "abox_dma.h"
#ifndef ABOX_CAPTURE_DMA_SLOTS
#define ABOX_CAPTURE_DMA_SLOTS 12U
#endif
#if ABOX_CAPTURE_DMA_SLOTS < 1U || ABOX_CAPTURE_DMA_SLOTS > 12U
#error Invalid ABOX_CAPTURE_DMA_SLOTS
#endif
typedef struct {
    const ABoxCaptureDmaEvents *events;ABoxDmaEvents dma_events;
    uint8_t id,channel,controller,dma_channel,opened,circular;
    volatile uint8_t fault,lost;
} CaptureDma;
static CaptureDma slots[ABOX_CAPTURE_DMA_SLOTS];
static CaptureDma *find(unsigned id,unsigned channel)
{ for(unsigned i=0U;i<ABOX_CAPTURE_DMA_SLOTS;++i)if(slots[i].id==id && slots[i].channel==channel)return &slots[i];return 0; }
static unsigned route(unsigned id,unsigned channel)
{
    static const uint8_t map[5][4]={{2U,3U,6U,4U},{5U,7U,1U,7U},
                                    {6U,0U,2U,3U},{1U,4U,5U,0U},{5U,4U,2U,1U}};
    return map[id-1U][channel-1U];
}
static void dma_event(void *context,unsigned controller,unsigned channel,ABoxDmaEvent event)
{
    CaptureDma *s=(CaptureDma *)context;ABoxCaptureDmaEvent out;const ABoxCaptureDmaEvents *e=s->events;
    (void)controller;(void)channel;
    if (s->lost || s->fault) { if(event==ABOX_DMA_TRANSFER_ERROR)s->fault=1U; return; }
    if (event==ABOX_DMA_TRANSFER_ERROR) s->fault=1U;
    if(ABoxCaptureHw_DmaOvercapture(s->id,s->channel)) {
        ABoxCaptureHw_DmaPause(s->id,s->channel);s->lost=1U;out=ABOX_CAPTURE_DMA_OVERCAPTURE;
    } else if(event==ABOX_DMA_TRANSFER_ERROR) {
        ABoxCaptureHw_DmaPause(s->id,s->channel);s->fault=1U;out=ABOX_CAPTURE_DMA_ERROR;
    } else if(event==ABOX_DMA_COMPLETE) {
        if(!s->circular)ABoxCaptureHw_DmaPause(s->id,s->channel);
        out=ABOX_CAPTURE_DMA_COMPLETE;
    } else out=ABOX_CAPTURE_DMA_HALF;
    if(e && e->on_event)e->on_event(e->context,s->id,s->channel,out);
}
ABoxHalStatus ABoxCapture_StartDma(unsigned id,unsigned channel,uint16_t *samples,uint16_t count,
                                   uint8_t circular,const ABoxCaptureDmaEvents *events)
{
    CaptureDma *s=0;ABoxHalStatus status;unsigned dma_channel,controller;
    ABoxDmaConfig c={ABOX_DMA_PERIPHERAL_TO_MEMORY,2U,2U,0U,1U,0U,1U};
    if(id<1U || id>5U || channel<1U || channel>4U || !samples || ((uintptr_t)samples&1U) || !count || circular>1U)
        return ABOX_HAL_INVALID;
    dma_channel=route(id,channel);if(!dma_channel)return ABOX_HAL_UNSUPPORTED;
    if(find(id,channel))return ABOX_HAL_BUSY;
    for(unsigned i=0U;i<ABOX_CAPTURE_DMA_SLOTS;++i)if(!slots[i].id){s=&slots[i];break;}
    if(!s)return ABOX_HAL_BUSY;
    status=ABoxCaptureHw_DmaReserve(id,channel);if(status!=ABOX_HAL_OK)return status;
    controller=id==5U ? 2U : 1U;
    s->id=(uint8_t)id;s->channel=(uint8_t)channel;s->controller=(uint8_t)controller;
    s->dma_channel=(uint8_t)dma_channel;s->opened=0U;s->circular=circular;s->fault=0U;s->lost=0U;
    s->events=events;s->dma_events.context=s;s->dma_events.on_event=dma_event;c.circular=circular;
    status=ABoxDma_Open(controller,dma_channel,&c);if(status!=ABOX_HAL_OK)goto failed;
    s->opened=1U;
    status=ABoxDma_Start(controller,dma_channel,ABoxCaptureHw_DmaAddress(id,channel),(uintptr_t)samples,count,&s->dma_events);
    if(status!=ABOX_HAL_OK)goto failed;
    ABoxCaptureHw_DmaEnable(id,channel);return ABOX_HAL_OK;
failed:
    if(!s->opened || ABoxDma_Close(controller,dma_channel)==ABOX_HAL_OK) {
        ABoxCaptureHw_DmaRelease(id,channel);s->id=0U;s->events=0;
    }
    return status;
}
ABoxHalStatus ABoxCapture_StopDma(unsigned id,unsigned channel,uint8_t *lost)
{
    CaptureDma *s=find(id,channel);ABoxHalStatus status;uint8_t missed,fault;
    if(id<1U || id>5U || channel<1U || channel>4U || !s || !lost)return ABOX_HAL_INVALID;
    ABoxCaptureHw_DmaPause(id,channel);
    missed=s->lost || ABoxCaptureHw_DmaOvercapture(id,channel);fault=s->fault;
    if(s->opened) {
        status=ABoxDma_Abort(s->controller,s->dma_channel);if(status!=ABOX_HAL_OK)return status;
        status=ABoxDma_Close(s->controller,s->dma_channel);if(status!=ABOX_HAL_OK)return status;
    }
    ABoxCaptureHw_DmaRelease(id,channel);s->id=0U;s->events=0;*lost=missed;
    return fault ? ABOX_HAL_ERROR : ABOX_HAL_OK;
}
