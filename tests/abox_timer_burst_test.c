#include <assert.h>
#include <stdint.h>
#include "abox_timer.h"
#include "abox_dma.h"
#include "abox_pwm_dma_backend.h"
#include "abox_timer_burst_backend.h"

static unsigned reserved,enabled,open_dma,controller_seen,channel_seen,configured_base,configured_length;
static unsigned callbacks,last_event,conflict,request_seen;
static uint16_t remaining;
static uintptr_t source_seen,destination_seen;
static ABoxDmaConfig dma_config;
static const ABoxDmaEvents *dma_events;
ABoxHalStatus ABoxTimerBurstHw_Reserve(unsigned id)
{ assert(id>=1U && id<=5U);if(reserved)return ABOX_HAL_BUSY;reserved=id;return ABOX_HAL_OK; }
void ABoxTimerBurstHw_Config(unsigned id,uint8_t base,uint8_t length)
{ assert(reserved==id);configured_base=base;configured_length=length; }
uintptr_t ABoxTimerBurstHw_Address(unsigned id) { assert(reserved==id);return 0x4000004cU; }
void ABoxTimerBurstHw_Release(unsigned id) { assert(reserved==id);reserved=0U; }
void ABoxTimerBurstHw_Request(unsigned id,unsigned request,int state)
{ assert(reserved==id);request_seen=request;enabled=(unsigned)state; }
ABoxHalStatus ABoxDma_Open(unsigned controller,unsigned channel,const ABoxDmaConfig *c)
{
    if(conflict)return ABOX_HAL_BUSY;assert(!open_dma);open_dma=1U;
    controller_seen=controller;channel_seen=channel;dma_config=*c;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Start(unsigned controller,unsigned channel,uintptr_t src,uintptr_t dst,uint16_t count,const ABoxDmaEvents *events)
{ assert(open_dma && controller==controller_seen && channel==channel_seen);source_seen=src;destination_seen=dst;remaining=count;dma_events=events;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Abort(unsigned controller,unsigned channel)
{ assert(open_dma && controller==controller_seen && channel==channel_seen);return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Remaining(unsigned controller,unsigned channel,uint16_t *count)
{ assert(open_dma && controller==controller_seen && channel==channel_seen);*count=remaining;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned controller,unsigned channel)
{ assert(open_dma && controller==controller_seen && channel==channel_seen);open_dma=0U;return ABOX_HAL_OK; }
static void event(void *context,unsigned id,ABoxTimerBurstEvent type)
{ assert(context==(void *)1 && id>=1U && id<=5U);++callbacks;last_event=(unsigned)type; }
static void fire(ABoxDmaEvent event) { dma_events->on_event(dma_events->context,controller_seen,channel_seen,event); }
int main(void)
{
    uint16_t words[12]={0};uint8_t partial=9U;ABoxTimerBurstEvents events={(void *)1,event};
    assert(ABoxTimer_StartBurstDma(1U,13U,4U,ABOX_TIMER_BURST_WRITE,words,6U,0U,&events)==ABOX_HAL_INVALID);
    assert(ABoxTimer_StartBurstDma(2U,10U,4U,ABOX_TIMER_BURST_WRITE,words,8U,0U,&events)==ABOX_HAL_UNSUPPORTED);
    assert(ABoxTimer_StartBurstDma(6U,13U,4U,ABOX_TIMER_BURST_WRITE,words,8U,0U,&events)==ABOX_HAL_UNSUPPORTED);
    assert(ABoxTimer_StartBurstDma(1U,13U,4U,ABOX_TIMER_BURST_WRITE,words,12U,1U,&events)==ABOX_HAL_INVALID);
    conflict=1U;assert(ABoxTimer_StartBurstDma(1U,13U,4U,ABOX_TIMER_BURST_WRITE,words,8U,0U,&events)==ABOX_HAL_BUSY && !reserved);conflict=0U;
    assert(ABoxTimer_StartBurstDma(1U,13U,4U,ABOX_TIMER_BURST_WRITE,words,8U,0U,&events)==ABOX_HAL_OK);
    assert(configured_base==13U && configured_length==4U && enabled && source_seen==(uintptr_t)words && destination_seen==0x4000004cU);
    assert(dma_config.direction==ABOX_DMA_MEMORY_TO_PERIPHERAL && dma_config.peripheral_width==2U && controller_seen==1U && channel_seen==5U);
    remaining=4U;fire(ABOX_DMA_HALF);assert(callbacks==1U && last_event==ABOX_TIMER_BURST_HALF);
    remaining=0U;fire(ABOX_DMA_COMPLETE);assert(callbacks==2U && last_event==ABOX_TIMER_BURST_COMPLETE && !enabled);
    assert(ABoxTimer_StopBurstDma(1U,&partial)==ABOX_HAL_OK && !partial && !reserved && !open_dma);
    assert(ABoxTimer_StartBurstDma(5U,13U,3U,ABOX_TIMER_BURST_READ,words,12U,1U,&events)==ABOX_HAL_OK);
    assert(controller_seen==2U && channel_seen==2U && dma_config.circular && source_seen==0x4000004cU && destination_seen==(uintptr_t)words);
    remaining=11U;assert(ABoxTimer_StopBurstDma(5U,&partial)==ABOX_HAL_OK && partial);
    assert(ABoxTimer_StartBurstDma(1U,13U,3U,ABOX_TIMER_BURST_READ,words,9U,0U,&events)==ABOX_HAL_OK);
    fire(ABOX_DMA_HALF);assert(callbacks==2U);fire(ABOX_DMA_TRANSFER_ERROR);
    assert(callbacks==3U && last_event==ABOX_TIMER_BURST_ERROR);
    assert(ABoxTimer_StopBurstDma(1U,&partial)==ABOX_HAL_ERROR && partial && !reserved);
    {
        static const uint8_t mapping[5][7]={
            {5U,2U,3U,6U,4U,4U,4U}, {2U,5U,7U,1U,7U,0U,0U},
            {3U,6U,0U,2U,3U,6U,0U}, {7U,1U,4U,5U,0U,0U,0U},
            {2U,5U,4U,2U,1U,1U,0U}
        };
        for (unsigned id=1U;id<=5U;++id) for (unsigned request=0U;request<=6U;++request) {
            ABoxHalStatus status=ABoxTimer_StartBurstDmaOn(id,(ABoxTimerBurstRequest)request,13U,1U,
                                        ABOX_TIMER_BURST_READ,words,2U,0U,0);
            if (!mapping[id-1U][request]) { assert(status==ABOX_HAL_UNSUPPORTED);continue; }
            assert(status==ABOX_HAL_OK && channel_seen==mapping[id-1U][request] &&
                   controller_seen==(id==5U ? 2U : 1U) && request_seen==request && enabled);
            assert(ABoxTimer_StopBurstDma(id,&partial)==ABOX_HAL_OK && !partial && !enabled);
        }
    }
    return 0;
}
