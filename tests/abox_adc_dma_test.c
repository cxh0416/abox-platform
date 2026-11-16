#include <assert.h>
#include <stdint.h>
#include "abox_adc_dma_backend.h"
#include "abox_platform_port.h"
static uint32_t tick(void *context) { (void)context;return 1U; }
static ABoxPlatformPort port={.get_tick_ms=tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return &port; }
static unsigned busy,prepared,started,paused,regular_paused,released,dma_open,notified,error,conflict,dual;
static unsigned injected_ready,injected_triggered,injected_masked,injected_events;
static unsigned dual_injected_events;
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t key) { assert(!key); }
ABoxHalStatus ABoxAdcHw_DmaTriggerInjected(unsigned id)
{ assert(id==1U);++injected_triggered;return ABOX_HAL_OK; }
uint8_t ABoxAdcHw_DmaInjectedFetch(unsigned id,uint8_t count,uint16_t *values)
{
 if (dual) { unsigned bit=1U<<(id-1U);assert(id<=2U && count==1U);
     if (!(injected_ready&bit)) return 0U;injected_ready&=~bit;values[0]=id==1U ? 0x123U : 0x456U;return 1U; }
 assert(id==1U && count==2U);if(!injected_ready)return 0U;injected_ready=0U;values[0]=0x123U;values[1]=0x456U;return 1U;
}
uint8_t ABoxAdcHw_DualInjectedFetch(uint8_t count,ABoxAdcDualSample *values)
{ assert(count==1U);if(!injected_ready)return 0U;injected_ready=0U;values[0].adc1=0x123U;values[0].adc2=0x456U;return 1U; }
static void dual_injected_event(void *context,const ABoxAdcDualSample *values,uint8_t count)
{ assert(context==(void *)3 && count==1U && values[0].adc1==0x123U && values[0].adc2==0x456U);++dual_injected_events; }
void ABoxAdcHw_DmaInjectedMask(unsigned id) { assert(id==1U || (dual && id==2U));++injected_masked; }
static void injected_event(void *context,unsigned id,const uint16_t *values,uint8_t count)
{ assert(context==(void *)2 && id==1U && count==2U && values[0]==0x123U && values[1]==0x456U);++injected_events; }
static const ABoxDmaEvents *pending;
ABoxHalStatus ABoxAdcHw_DmaReserve(unsigned id)
{ if(id==2U)return ABOX_HAL_UNSUPPORTED;if(busy)return ABOX_HAL_BUSY;busy=1U;return ABOX_HAL_OK; }
ABoxHalStatus ABoxAdcHw_DmaPrepare(unsigned id,const ABoxAdcDmaConfig *c)
{ assert(id==1U && c->entry_count==2U);++prepared;return ABOX_HAL_OK; }
uintptr_t ABoxAdcHw_DmaAddress(unsigned id) { assert(id==1U);return 0x4001244cU; }
ABoxHalStatus ABoxAdcHw_DmaStart(unsigned id,const ABoxAdcDmaConfig *c)
{ assert(id==1U && c);++started;return ABOX_HAL_OK; }
void ABoxAdcHw_DmaPause(unsigned id) { assert(id==1U);++paused; }
ABoxHalStatus ABoxAdcHw_DmaRelease(unsigned id,int fault)
{ assert(id==1U);error=(unsigned)fault;busy=0U;++released;return ABOX_HAL_OK; }
ABoxHalStatus ABoxAdcHw_DualReserve(void) { if(busy)return ABOX_HAL_BUSY;busy=1U;dual=1U;return ABOX_HAL_OK; }
ABoxHalStatus ABoxAdcHw_DualPrepare(const ABoxAdcDualDmaConfig *c) { assert(dual && c->entry_count>=1U);++prepared;return ABOX_HAL_OK; }
ABoxHalStatus ABoxAdcHw_DualStart(const ABoxAdcDualDmaConfig *c) { assert(dual && c);++started;return ABOX_HAL_OK; }
void ABoxAdcHw_DualPause(void) { assert(dual);++paused; }
void ABoxAdcHw_DualRegularPause(void) { assert(dual);++regular_paused; }
ABoxHalStatus ABoxAdcHw_DualRelease(int fault) { assert(dual);error=(unsigned)fault;busy=0U;dual=0U;++released;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Open(unsigned c,unsigned ch,const ABoxDmaConfig *cfg)
{ assert(c==1U&&ch==1U&&cfg->memory_width==(dual?4U:2U)&&cfg->peripheral_width==cfg->memory_width&&cfg->direction==ABOX_DMA_PERIPHERAL_TO_MEMORY);if(conflict)return ABOX_HAL_BUSY;dma_open=1U;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Start(unsigned c,unsigned ch,uintptr_t src,uintptr_t dst,uint16_t count,const ABoxDmaEvents *e)
{ assert(c==1U&&ch==1U&&src==0x4001244cU&&dst&&count==4U);pending=e;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Abort(unsigned c,unsigned ch) { assert(c==1U&&ch==1U&&dma_open);return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned c,unsigned ch) { assert(c==1U&&ch==1U&&dma_open);dma_open=0U;return ABOX_HAL_OK; }
static void event(void *ctx,ABoxDmaEvent e) { assert(ctx==(void*)1 && e==ABOX_DMA_COMPLETE);++notified; }
int main(void)
{
 ABoxAdcSequenceEntry entries[]={{3U,ABOX_ADC_SAMPLE_7_5},{5U,ABOX_ADC_SAMPLE_28_5}};
 ABoxAdcDmaConfig c={entries,2U,ABOX_ADC_TRIG_TIM3_TRGO,0U,1U,0};
 ABoxAdcDmaEvents e={(void*)1,event};uint16_t samples[4];
 assert(ABoxAdc_StartDma(2U,&c,samples,4U,&e)==ABOX_HAL_UNSUPPORTED);
 assert(ABoxAdc_StartDma(1U,&c,samples,3U,&e)==ABOX_HAL_INVALID);
 c.continuous=1U;assert(ABoxAdc_StartDma(1U,&c,samples,4U,&e)==ABOX_HAL_INVALID);c.continuous=0U;
 entries[1].channel=3U;assert(ABoxAdc_StartDma(1U,&c,samples,4U,&e)==ABOX_HAL_INVALID);entries[1].channel=5U;
 conflict=1U;assert(ABoxAdc_StartDma(1U,&c,samples,4U,&e)==ABOX_HAL_BUSY && !busy);conflict=0U;
 assert(ABoxAdc_StartDma(1U,&c,samples,4U,&e)==ABOX_HAL_OK && dma_open && prepared==1U && started==1U);
 pending->on_event(pending->context,1U,1U,ABOX_DMA_COMPLETE);assert(notified==1U && paused==1U);
 assert(ABoxAdc_StopDma(1U)==ABOX_HAL_OK && !dma_open && !busy && !error);
 c.circular=0U;c.trigger=ABOX_ADC_TRIG_SOFTWARE;c.continuous=1U;
 assert(ABoxAdc_StartDma(1U,&c,samples,4U,0)==ABOX_HAL_OK);
 pending->on_event(pending->context,1U,1U,ABOX_DMA_TRANSFER_ERROR);
 assert(ABoxAdc_StopDma(1U)==ABOX_HAL_OK && error && released>=3U);
 {
  ABoxAdcDualEntry both[]={{3U,4U,ABOX_ADC_SAMPLE_7_5},{5U,6U,ABOX_ADC_SAMPLE_28_5}};
  ABoxAdcDualDmaConfig d={both,2U,ABOX_ADC_TRIG_TIM3_TRGO,0U,1U,ABOX_ADC_DUAL_SIMULTANEOUS,0};uint32_t pairs[4];
  assert(ABoxAdc_StartDualDma(&d,pairs,3U,&e)==ABOX_HAL_INVALID);
  both[0].adc2_channel=3U;assert(ABoxAdc_StartDualDma(&d,pairs,4U,&e)==ABOX_HAL_INVALID);both[0].adc2_channel=4U;
  both[1].adc2_channel=4U;assert(ABoxAdc_StartDualDma(&d,pairs,4U,&e)==ABOX_HAL_INVALID);both[1].adc2_channel=6U;
  conflict=1U;assert(ABoxAdc_StartDualDma(&d,pairs,4U,&e)==ABOX_HAL_BUSY && !busy && !dual);conflict=0U;
  assert(ABoxAdc_StartDualDma(&d,pairs,4U,&e)==ABOX_HAL_OK && busy && dual && dma_open);
  assert(ABoxAdc_StartDma(1U,&c,samples,4U,0)==ABOX_HAL_BUSY && ABoxAdc_StopDma(1U)==ABOX_HAL_INVALID);
  pending->on_event(pending->context,1U,1U,ABOX_DMA_COMPLETE);assert(notified==2U);
  assert(ABoxAdc_StopDualDma()==ABOX_HAL_OK && !busy && !dual && !dma_open && !error);
  {
   ABoxAdcDualEntry injected[]={{7U,8U,ABOX_ADC_SAMPLE_28_5}};
   ABoxAdcDualLinkedEvents ie={(void *)3,dual_injected_event};
   ABoxAdcDualLinkedConfig linked={injected,1U,ABOX_ADC_INJ_SOFTWARE,&ie};
   ABoxAdcDualSample sample;uint32_t generation=0U;d.linked=&linked;
   assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
   assert(ABoxAdc_GetDualLinkedInjected(&sample,1U,&generation)==ABOX_HAL_BUSY);
   assert(ABoxAdc_TriggerDualLinkedInjected()==ABOX_HAL_OK && ABoxAdc_TriggerDualLinkedInjected()==ABOX_HAL_BUSY);
   assert(ABoxAdc_StopDualDma()==ABOX_HAL_BUSY);
   injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);
   assert(dual_injected_events==1U && ABoxAdc_GetDualLinkedInjected(&sample,1U,&generation)==ABOX_HAL_OK &&
          sample.adc1==0x123U && sample.adc2==0x456U && generation==1U);
   assert(ABoxAdc_StopDualDma()==ABOX_HAL_OK);
   linked.mode=ABOX_ADC_DUAL_INJECT_ALTERNATE;
   assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
   assert(ABoxAdc_TriggerDualLinkedInjected()==ABOX_HAL_OK);
   injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);
   assert(ABoxAdc_GetDualLinkedInjected(&sample,1U,&generation)==ABOX_HAL_BUSY);
   assert(ABoxAdc_TriggerDualLinkedInjected()==ABOX_HAL_OK);
   injected_ready=2U;ABoxAdcDma_InjectedIrq(2U);
   assert(dual_injected_events==2U && ABoxAdc_GetDualLinkedInjected(&sample,1U,&generation)==ABOX_HAL_OK &&
          sample.adc1==0x123U && sample.adc2==0x456U && generation==1U);
   assert(ABoxAdc_StopDualDma()==ABOX_HAL_OK);
   linked.automatic=1U;
   assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_INVALID);
   linked.mode=ABOX_ADC_DUAL_INJECT_SIMULTANEOUS;
   assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
   assert(ABoxAdc_TriggerDualLinkedInjected()==ABOX_HAL_INVALID);
   injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);
   assert(dual_injected_events==3U && ABoxAdc_StopDualDma()==ABOX_HAL_OK);
   d.circular=0U;d.continuous=1U;d.trigger=ABOX_ADC_TRIG_SOFTWARE;
   assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
   pending->on_event(pending->context,1U,1U,ABOX_DMA_COMPLETE);
   assert(regular_paused==1U && ABoxAdc_StopDualDma()==ABOX_HAL_BUSY);
   injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);assert(ABoxAdc_StopDualDma()==ABOX_HAL_BUSY);
   injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);assert(ABoxAdc_StopDualDma()==ABOX_HAL_OK);
   assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
   pending->on_event(pending->context,1U,1U,ABOX_DMA_COMPLETE);
   assert(ABoxAdc_StopDualDma()==ABOX_HAL_BUSY && ABoxAdc_AbortDualDma()==ABOX_HAL_OK && error);
   d.linked=0;injected_triggered=0U;
  }
  d.circular=0U;d.continuous=1U;d.trigger=ABOX_ADC_TRIG_SOFTWARE;
  assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
  pending->on_event(pending->context,1U,1U,ABOX_DMA_TRANSFER_ERROR);
  assert(ABoxAdc_StopDualDma()==ABOX_HAL_OK && error && !busy && !dual);
  d.entry_count=1U;d.mode=ABOX_ADC_DUAL_INTERLEAVED_FAST;d.trigger=ABOX_ADC_TRIG_TIM3_TRGO;
  both[0].adc2_channel=3U;both[0].sample=ABOX_ADC_SAMPLE_7_5;
  assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_INVALID);
  both[0].sample=ABOX_ADC_SAMPLE_1_5;
  assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
  assert(ABoxAdc_StopDualDma()==ABOX_HAL_OK && !busy && !dual);
  d.mode=ABOX_ADC_DUAL_INTERLEAVED_SLOW;
  assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_INVALID);
  d.continuous=0U;d.trigger=ABOX_ADC_TRIG_SOFTWARE;both[0].sample=ABOX_ADC_SAMPLE_28_5;
  assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_INVALID);
  both[0].sample=ABOX_ADC_SAMPLE_13_5;
  assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_OK);
  assert(ABoxAdc_StopDualDma()==ABOX_HAL_OK && !busy && !dual);
  both[0].adc2_channel=4U;assert(ABoxAdc_StartDualDma(&d,pairs,4U,0)==ABOX_HAL_INVALID);
 }
 {
  ABoxAdcLinkedEvents ie={(void *)2,injected_event};
  ABoxAdcSequenceEntry extra[]={{3U,ABOX_ADC_SAMPLE_7_5},{8U,ABOX_ADC_SAMPLE_13_5}};
  ABoxAdcLinkedConfig link={extra,2U,ABOX_ADC_INJECT_AFTER_REGULAR,ABOX_ADC_INJ_SOFTWARE,&ie};
  uint16_t latest[2]={0U,0U};uint32_t generation=0U;
  c.linked=&link;c.continuous=0U;c.circular=0U;c.trigger=ABOX_ADC_TRIG_TIM3_TRGO;
  extra[0].sample=ABOX_ADC_SAMPLE_28_5;assert(ABoxAdc_StartDma(1U,&c,samples,4U,0)==ABOX_HAL_INVALID);extra[0].sample=ABOX_ADC_SAMPLE_7_5;
  assert(ABoxAdc_StartDma(1U,&c,samples,4U,0)==ABOX_HAL_OK);
  assert(ABoxAdc_GetLinkedInjected(1U,latest,2U,&generation)==ABOX_HAL_BUSY);
  pending->on_event(pending->context,1U,1U,ABOX_DMA_COMPLETE);
  assert(ABoxAdc_StopDma(1U)==ABOX_HAL_BUSY);
  injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);
  assert(ABoxAdc_StopDma(1U)==ABOX_HAL_BUSY && injected_events==1U);
  assert(ABoxAdc_GetLinkedInjected(1U,latest,2U,&generation)==ABOX_HAL_OK && generation==1U && latest[0]==0x123U && latest[1]==0x456U);
  injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);
  assert(ABoxAdc_StopDma(1U)==ABOX_HAL_OK && injected_events==2U && injected_masked);
  link.mode=ABOX_ADC_INJECT_TRIGGERED;c.circular=1U;
  assert(ABoxAdc_StartDma(1U,&c,samples,4U,0)==ABOX_HAL_OK);
  assert(ABoxAdc_TriggerLinkedInjected(1U)==ABOX_HAL_OK && injected_triggered==1U);
  assert(ABoxAdc_TriggerLinkedInjected(1U)==ABOX_HAL_BUSY);
  injected_ready=1U;ABoxAdcDma_InjectedIrq(1U);
  assert(ABoxAdc_TriggerLinkedInjected(1U)==ABOX_HAL_OK && injected_triggered==2U);
  assert(ABoxAdc_AbortDma(1U)==ABOX_HAL_OK && error);
  assert(ABoxAdc_GetLinkedInjected(1U,latest,2U,&generation)==ABOX_HAL_INVALID);
 }
 return 0;
}
