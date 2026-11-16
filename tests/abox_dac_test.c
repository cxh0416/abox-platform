#include <assert.h>
#include "abox_dac_backend.h"
static unsigned opens, enables, disables, writes, triggers;
static unsigned dma_request, dma_open, dma_start, dma_abort, dma_close, dma_notified;
static const ABoxDmaEvents *pending;
static ABoxHalStatus dma_result=ABOX_HAL_OK;
uintptr_t ABoxDacHw_DmaAddress(unsigned ch) { return 0x40007400U+ch*4U; }
void ABoxDacHw_DmaRequest(unsigned ch,int enabled) { (void)ch; dma_request=enabled; }
ABoxHalStatus ABoxDma_Open(unsigned c,unsigned ch,const ABoxDmaConfig *v)
{ assert(c==2U && ch==3U && v->direction==ABOX_DMA_MEMORY_TO_PERIPHERAL && v->memory_width==2U); ++dma_open; return dma_result; }
ABoxHalStatus ABoxDma_Start(unsigned c,unsigned ch,uintptr_t src,uintptr_t dst,uint16_t n,const ABoxDmaEvents *e)
{ assert(c==2U && ch==3U && src && dst==0x40007404U && n==2U);++dma_start;pending=e;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Abort(unsigned c,unsigned ch) { assert(c==2U && ch==3U);++dma_abort;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned c,unsigned ch) { assert(c==2U && ch==3U);++dma_close;return ABOX_HAL_OK; }
static void dma_done(void *ctx,unsigned ch,ABoxDmaEvent e)
{ assert(ctx==(void *)1 && ch==1U && e==ABOX_DMA_COMPLETE);++dma_notified; }
static uint16_t values[2]; static ABoxHalStatus result=ABOX_HAL_OK;
ABoxHalStatus ABoxDacHw_Open(unsigned ch,const ABoxDacConfig *c) { ++opens;values[ch-1U]=c->initial_value;return result; }
void ABoxDacHw_Enable(unsigned ch) { assert(ch==1U);++enables; }
void ABoxDacHw_Disable(unsigned ch) { assert(ch==1U);++disables; }
void ABoxDacHw_Write(unsigned ch,uint16_t v) { ++writes;values[ch-1U]=v; }
void ABoxDacHw_Trigger(unsigned ch) { assert(ch==1U);++triggers; }
uint16_t ABoxDacHw_ReadOutput(unsigned ch) { return values[ch-1U]; }
int main(void)
{
 ABoxDacConfig c={ABOX_DAC_SOFTWARE,1U,123U,ABOX_DAC_WAVE_NONE,0U}; uint16_t v=999U, samples[]={1U,4095U};
 ABoxDacDmaEvents e={(void *)1,dma_done};
 assert(ABoxDac_Open(0,&c)==ABOX_HAL_INVALID && ABoxDac_Open(3,&c)==ABOX_HAL_INVALID && !opens);
 c.initial_value=4096U; assert(ABoxDac_Open(1,&c)==ABOX_HAL_INVALID); c.initial_value=123U;
 c.output_buffer=2;assert(ABoxDac_Open(1,&c)==ABOX_HAL_INVALID);c.output_buffer=1;
 c.wave=ABOX_DAC_WAVE_TRIANGLE;c.wave_bits=0U;assert(ABoxDac_Open(1,&c)==ABOX_HAL_INVALID);
 c.wave_bits=13U;assert(ABoxDac_Open(1,&c)==ABOX_HAL_INVALID);
 c.wave_bits=12U;c.trigger=ABOX_DAC_IMMEDIATE;assert(ABoxDac_Open(1,&c)==ABOX_HAL_INVALID);
 c.trigger=ABOX_DAC_SOFTWARE;c.wave=ABOX_DAC_WAVE_NONE;assert(ABoxDac_Open(1,&c)==ABOX_HAL_INVALID);c.wave_bits=0U;
 result=ABOX_HAL_ERROR;assert(ABoxDac_Open(1,&c)==ABOX_HAL_ERROR);result=ABOX_HAL_OK;
 assert(ABoxDac_Open(1,&c)==ABOX_HAL_OK && ABoxDac_Open(1,&c)==ABOX_HAL_BUSY);
 assert(ABoxDac_Trigger(1)==ABOX_HAL_INVALID && ABoxDac_Enable(1)==ABOX_HAL_OK && enables==1U);
 assert(ABoxDac_Enable(1)==ABOX_HAL_BUSY && ABoxDac_Trigger(1)==ABOX_HAL_OK && triggers==1U);
 assert(ABoxDac_Write(1,4096U)==ABOX_HAL_INVALID && !writes);
 assert(ABoxDac_Write(1,4095U)==ABOX_HAL_OK && ABoxDac_ReadOutput(1,&v)==ABOX_HAL_OK && v==4095U);
 dma_result=ABOX_HAL_BUSY;assert(ABoxDac_StartDma(1,samples,2U,0U,&e)==ABOX_HAL_BUSY && !dma_request);
 dma_result=ABOX_HAL_OK;assert(ABoxDac_StartDma(1,samples,2U,0U,&e)==ABOX_HAL_OK && dma_request);
 assert(ABoxDac_Write(1,2U)==ABOX_HAL_BUSY && ABoxDac_StartDma(1,samples,2U,0U,&e)==ABOX_HAL_BUSY);
 pending->on_event(pending->context,2U,3U,ABOX_DMA_COMPLETE);assert(!dma_request && dma_notified==1U);
 assert(ABoxDac_StopDma(1)==ABOX_HAL_OK && dma_abort==1U && dma_close==1U);
 assert(ABoxDac_StartDma(1,samples,2U,1U,&e)==ABOX_HAL_OK && dma_request);
 pending->on_event(pending->context,2U,3U,ABOX_DMA_COMPLETE);assert(dma_request && dma_notified==2U);
 assert(ABoxDac_Disable(1)==ABOX_HAL_OK && !dma_request && dma_abort==2U && dma_close==2U && disables==1U && ABoxDac_Disable(1)==ABOX_HAL_OK && disables==1U);
 assert(ABoxDac_Trigger(1)==ABOX_HAL_INVALID && ABoxDac_Enable(1)==ABOX_HAL_OK);
 assert(ABoxDac_Close(1)==ABOX_HAL_OK && disables==2U && ABoxDac_Close(1)==ABOX_HAL_INVALID);
 v=999U;assert(ABoxDac_ReadOutput(1,&v)==ABOX_HAL_INVALID && v==999U);
 c.wave=ABOX_DAC_WAVE_NOISE;c.wave_bits=8U;
 assert(ABoxDac_Open(1,&c)==ABOX_HAL_OK && ABoxDac_Enable(1)==ABOX_HAL_OK);
 assert(ABoxDac_StartDma(1,samples,2U,1U,&e)==ABOX_HAL_OK && dma_request);
 assert(ABoxDac_Close(1)==ABOX_HAL_OK && !dma_request);
 c.trigger=ABOX_DAC_TIM3;c.wave=ABOX_DAC_WAVE_TRIANGLE;c.wave_bits=12U;
 assert(ABoxDac_Open(2,&c)==ABOX_HAL_OK && ABoxDac_Trigger(2)==ABOX_HAL_INVALID);
 assert(ABoxDac_Close(2)==ABOX_HAL_OK);return 0;
}
