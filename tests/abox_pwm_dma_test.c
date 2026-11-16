#include <assert.h>
#include <stdint.h>
#include "abox_pwm_dma_backend.h"
#include "abox_timer.h"
#include "abox_dma.h"

static unsigned reserved[6], running[6], dma_owned[3][8], dma_active[3][8], dma_circular[3][8];
static unsigned fail_start, fail_prepare, events;
static ABoxPwmDmaEvent last_event;
static const ABoxDmaEvents *pending[3][8];

ABoxHalStatus ABoxPwmHw_DmaReserve(unsigned id,unsigned ch,uint16_t initial,const uint16_t *samples,uint16_t count)
{
    assert(id<=5U && ch<=4U && samples && count && initial<=100U);
    for(unsigned i=0;i<count;++i) if(samples[i]>100U)return ABOX_HAL_INVALID;
    if(reserved[id])return ABOX_HAL_BUSY;
    reserved[id]=ch;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxPwmHw_DmaPrepare(unsigned id,unsigned ch,uint16_t initial)
{ assert(reserved[id]==ch && initial<=100U);return fail_prepare ? ABOX_HAL_ERROR : ABOX_HAL_OK; }
uintptr_t ABoxPwmHw_DmaAddress(unsigned id,unsigned ch) { return 0x40000000U+id*1024U+ch*4U; }
void ABoxPwmHw_DmaEnable(unsigned id) { assert(reserved[id]);running[id]=1U; }
void ABoxPwmHw_DmaPause(unsigned id) { running[id]=0U; }
void ABoxPwmHw_DmaRelease(unsigned id,unsigned ch) { assert(reserved[id]==ch);running[id]=reserved[id]=0U; }
ABoxHalStatus ABoxDma_Open(unsigned ctl,unsigned ch,const ABoxDmaConfig *c)
{
    assert(c->direction==ABOX_DMA_MEMORY_TO_PERIPHERAL && c->peripheral_width==2U && c->memory_width==2U);
    if(dma_owned[ctl][ch])return ABOX_HAL_BUSY;
    dma_owned[ctl][ch]=1U;dma_circular[ctl][ch]=c->circular;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Start(unsigned ctl,unsigned ch,uintptr_t src,uintptr_t dst,uint16_t n,const ABoxDmaEvents *e)
{
    assert(dma_owned[ctl][ch] && src && dst && n==4U);
    if(fail_start)return ABOX_HAL_ERROR;
    dma_active[ctl][ch]=1U;pending[ctl][ch]=e;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Abort(unsigned ctl,unsigned ch)
{ assert(dma_owned[ctl][ch]);dma_active[ctl][ch]=0U;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned ctl,unsigned ch)
{
    assert(dma_owned[ctl][ch] && !dma_active[ctl][ch]);
    dma_owned[ctl][ch]=0U;pending[ctl][ch]=0;return ABOX_HAL_OK;
}
static void event(void *context,unsigned id,unsigned ch,ABoxPwmDmaEvent value)
{ assert(context==(void*)1 && id==1U && ch==1U);++events;last_event=value; }
static void dispatch(unsigned ctl,unsigned ch,ABoxDmaEvent value)
{
    const ABoxDmaEvents *e=pending[ctl][ch];
    assert(e && dma_active[ctl][ch]);
    if(value==ABOX_DMA_TRANSFER_ERROR || (value==ABOX_DMA_COMPLETE && !dma_circular[ctl][ch]))
        dma_active[ctl][ch]=0U;
    e->on_event(e->context,ctl,ch,value);
}
int main(void)
{
    static const uint8_t routes[5]={5U,2U,3U,7U,2U};
    const uint16_t samples[4]={0U,50U,100U,25U}, invalid[4]={0U,101U,0U,0U};
    ABoxPwmDmaEvents callbacks={(void*)1,event};
    assert(ABoxPwm_StopDma(0U,0U)==ABOX_HAL_INVALID);
    assert(ABoxPwm_StartDma(1U,1U,0U,invalid,4U,0U,0)==ABOX_HAL_INVALID);
    for(unsigned id=1;id<=5U;++id) {
        unsigned ctl=id==5U?2U:1U, route=routes[id-1U];
        assert(ABoxPwm_StartDma(id,3U,5U,samples,4U,0U,0)==ABOX_HAL_OK);
        assert(dma_owned[ctl][route] && running[id] && reserved[id]==3U);
        assert(ABoxPwm_StopDma(id,3U)==ABOX_HAL_OK && !dma_owned[ctl][route] && !reserved[id]);
    }
    dma_owned[1][2]=1U;
    assert(ABoxPwm_StartDma(2U,1U,5U,samples,4U,0U,0)==ABOX_HAL_BUSY && !reserved[2]);
    dma_owned[1][2]=0U;
    fail_start=1U;
    assert(ABoxPwm_StartDma(2U,1U,5U,samples,4U,0U,0)==ABOX_HAL_ERROR && !reserved[2] && !dma_owned[1][2]);
    fail_start=0U;fail_prepare=1U;
    assert(ABoxPwm_StartDma(2U,1U,5U,samples,4U,0U,0)==ABOX_HAL_ERROR && !reserved[2] && !dma_owned[1][2]);
    fail_prepare=0U;
    assert(ABoxPwm_StartDma(1U,1U,5U,samples,4U,1U,&callbacks)==ABOX_HAL_OK);
    assert(ABoxPwm_StartDma(1U,2U,5U,samples,4U,1U,0)==ABOX_HAL_BUSY);
    dispatch(1U,5U,ABOX_DMA_HALF);assert(events==1U && last_event==ABOX_PWM_DMA_HALF && running[1]);
    dispatch(1U,5U,ABOX_DMA_COMPLETE);assert(events==2U && last_event==ABOX_PWM_DMA_COMPLETE && running[1]);
    dispatch(1U,5U,ABOX_DMA_TRANSFER_ERROR);assert(events==3U && last_event==ABOX_PWM_DMA_ERROR && !running[1]);
    assert(ABoxPwm_StopDma(1U,1U)==ABOX_HAL_ERROR && !reserved[1] && !dma_owned[1][5]);
    assert(ABoxPwm_StartDma(1U,1U,5U,samples,4U,0U,&callbacks)==ABOX_HAL_OK);
    dispatch(1U,5U,ABOX_DMA_COMPLETE);assert(last_event==ABOX_PWM_DMA_COMPLETE && !running[1]);
    assert(ABoxPwm_StopDma(1U,1U)==ABOX_HAL_OK);
    return 0;
}
