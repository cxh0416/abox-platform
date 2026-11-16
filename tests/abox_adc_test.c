#include <assert.h>
#include <stdint.h>
#include "abox_adc.h"
#include "abox_adc_dma_backend.h"
#include "abox_adc_dual_injected_backend.h"
#include "abox_platform_port.h"

static unsigned clocks[3],tick_ready=1,calibration_timeout,conversion_timeout,selected_channel,selected_sample,shared_divider;
static unsigned injected_entries,selected_trigger,injected_software,dual_mode;
static unsigned selected_auto;
static unsigned selected_scan[2];
static unsigned watchdog_flags[2],watchdog_callbacks;
#ifndef TEST_STM32
static unsigned adc_enabled[2],injected_trigger_enabled;
#endif
static uint32_t now,adc_pclk=72000000U;
static uint32_t fake_tick(void *p) { (void)p; return now; }
static ABoxPlatformPort port={.get_tick_ms=fake_tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return tick_ready?&port:0; }
void fake_adc_clock(unsigned id,unsigned on) { clocks[id]=on; }
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t saved) { assert(saved==0U); }
void ABoxAdcDma_InjectedIrq(unsigned id) { (void)id; }
static void watchdog(void *ctx,unsigned id) { assert(ctx==(void *)1 && id==1U);++watchdog_callbacks; }

#ifdef TEST_STM32
#include "stm32f1xx_hal_adc.h"
ADC_TypeDef fake_adc_st[2]={{1},{2}};
uint32_t HAL_RCC_GetPCLK2Freq(void) { return adc_pclk; }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *p) { shared_divider=p->AdcClockSelection;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h) { assert(h->Instance);selected_scan[h->Instance->id-1U]=h->Init.ScanConvMode;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef *h) { assert(h->Instance);return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h) { (void)h;return calibration_timeout?HAL_TIMEOUT:HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h,ADC_ChannelConfTypeDef *c)
{ (void)h;selected_channel=c->Channel;selected_sample=c->SamplingTime;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h,uint32_t timeout)
{ (void)h;(void)timeout;return conversion_timeout?HAL_TIMEOUT:HAL_OK; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) { (void)h;return 0x0a5bU; }
uint32_t fake_adc_injected_flag(ADC_HandleTypeDef *h,uint32_t flag)
{ assert(h);if(flag==ADC_FLAG_AWD)return watchdog_flags[h->Instance->id-1U];assert(flag==ADC_FLAG_JEOC);return conversion_timeout ? 0U : flag; }
void fake_adc_clear_flag(ADC_HandleTypeDef *h,uint32_t flag)
{ assert(h);if(flag==ADC_FLAG_AWD)watchdog_flags[h->Instance->id-1U]=0U;else assert(flag==ADC_FLAG_JEOC); }
HAL_StatusTypeDef HAL_ADCEx_InjectedConfigChannel(ADC_HandleTypeDef *h,ADC_InjectionConfTypeDef *c)
{ assert(h && c->InjectedRank>=1U && c->InjectedRank<=c->InjectedNbrOfConversion && !c->InjectedOffset);
  selected_trigger=c->ExternalTrigInjecConv;selected_auto=c->AutoInjectedConv;
  if(selected_auto) h->Instance->CR1|=ADC_CR1_JAUTO;
  ++injected_entries;return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_InjectedStart(ADC_HandleTypeDef *h)
{ assert(h);injected_software=selected_trigger==(7U<<12U);return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_InjectedStop(ADC_HandleTypeDef *h) { assert(h);return HAL_OK; }
uint32_t HAL_ADCEx_InjectedGetValue(ADC_HandleTypeDef *h,uint32_t rank) { assert(h && rank<=4U);return 0x110U+rank; }
HAL_StatusTypeDef HAL_ADCEx_MultiModeConfigChannel(ADC_HandleTypeDef *h,ADC_MultiModeTypeDef *mode)
{ assert(h->Instance==ADC1);dual_mode=mode->Mode;return HAL_OK; }
uint32_t HAL_ADCEx_MultiModeGetValue(ADC_HandleTypeDef *h) { assert(h->Instance==ADC1 && dual_mode);return 0x02340123U; }
uint32_t ABox_PortGetTickMs(void) { return now++; }
#else
#include "gd32f10x_adc.h"
uint32_t fake_adc_ctl[2], fake_adc_data[2], fake_adc_ctl0[2], fake_adc_low[2], fake_adc_high[2];
uint32_t ABox_PortGetTickMs(void)
{
    if (!calibration_timeout) { fake_adc_ctl[0]=0;fake_adc_ctl[1]=0; }
    return now++;
}
void rcu_periph_clock_enable(uint32_t id) { fake_adc_clock(id,1); }
void rcu_periph_clock_disable(uint32_t id) { fake_adc_clock(id,0); }
uint32_t rcu_clock_freq_get(uint32_t clock) { assert(clock==CK_APB2);return adc_pclk; }
void rcu_adc_clock_config(uint32_t divider) { shared_divider=divider; }
void adc_mode_config(uint32_t mode) { assert((mode==ADC_MODE_FREE || mode==ADC_DAUL_REGULAL_PARALLEL ||
    mode==ADC_DAUL_REGULAL_FOLLOWUP_FAST || mode==ADC_DAUL_REGULAL_FOLLOWUP_SLOW || mode==ADC_DAUL_INSERTED_PARALLEL ||
    mode==ADC_DAUL_REGULAL_PARALLEL_INSERTED_PARALLEL || mode==ADC_DAUL_INSERTED_PARALLEL_REGULAL_FOLLOWUP_FAST ||
    mode==ADC_DAUL_INSERTED_PARALLEL_REGULAL_FOLLOWUP_SLOW || mode==ADC_DAUL_INSERTED_TRIGGER_ROTATION ||
    mode==ADC_DAUL_REGULAL_PARALLEL_INSERTED_ROTATION) && clocks[1]);dual_mode=mode; }
void adc_deinit(uint32_t id) { fake_adc_ctl[id-1U]=0; }
void adc_data_alignment_config(uint32_t id,uint32_t align) { (void)id;(void)align; }
void adc_special_function_config(uint32_t id,uint32_t f,ControlStatus state)
{ if(f==ADC_INSERTED_CHANNEL_AUTO)selected_auto=(unsigned)state;if(f & ADC_SCAN_MODE)selected_scan[id-1U]=(unsigned)state; }
void adc_channel_length_config(uint32_t id,uint8_t group,uint32_t n) { (void)id;(void)group;assert(n>=1U && n<=16U); }
void adc_external_trigger_source_config(uint32_t id,uint8_t group,uint32_t source)
{ (void)id;if(group==ADC_INSERTED_CHANNEL)selected_trigger=source; }
void adc_external_trigger_config(uint32_t id,uint8_t group,ControlStatus state)
{ (void)id;if(group==ADC_INSERTED_CHANNEL)injected_trigger_enabled=(unsigned)state; }
void adc_dma_mode_enable(uint32_t id) { assert(id==ADC0); }
void adc_dma_mode_disable(uint32_t id) { assert(id==ADC0); }
void adc_enable(uint32_t id) { adc_enabled[id-1U]=1U; }
void adc_disable(uint32_t id) { adc_enabled[id-1U]=0U; }
void adc_tempsensor_vrefint_enable(void) { }
void adc_tempsensor_vrefint_disable(void) { }
void adc_regular_channel_config(uint32_t id,uint8_t rank,uint8_t ch,uint32_t sample)
{ (void)id;assert(rank<16U);selected_channel=ch;selected_sample=sample; }
void adc_inserted_channel_config(uint32_t id,uint8_t rank,uint8_t ch,uint32_t sample)
{ (void)id;(void)ch;(void)sample;assert(rank<4U);++injected_entries; }
void adc_inserted_channel_offset_config(uint32_t id,uint8_t rank,uint16_t offset)
{ (void)id;assert(rank<4U && !offset); }
uint16_t adc_inserted_data_read(uint32_t id,uint8_t rank) { (void)id;assert(rank<4U);return (uint16_t)(0x111U+rank); }
void adc_flag_clear(uint32_t id,uint32_t f) { if(f==ADC_FLAG_WDE)watchdog_flags[id-1U]=0U; }
void adc_software_trigger_enable(uint32_t id,uint8_t group) { (void)id;if(group==ADC_INSERTED_CHANNEL)injected_software=1U; }
FlagStatus adc_flag_get(uint32_t id,uint32_t f) { if(f==ADC_FLAG_WDE)return watchdog_flags[id-1U]?SET:RESET;return conversion_timeout?RESET:SET; }
uint16_t adc_regular_data_read(uint32_t id) { (void)id;return 0x0a5bU; }
uint32_t adc_sync_mode_convert_value_read(void) { assert(dual_mode);return 0x02340123U; }
#endif

int main(void)
{
    ABoxAdcConfig c={6U}, mismatch={8U};
    uint16_t value=0xdeadU;
    assert(ABoxAdc_Open(0,&c)==ABOX_HAL_INVALID);
    c.clock_divider=2U;assert(ABoxAdc_Open(1,&c)==ABOX_HAL_INVALID);c.clock_divider=6U;
    tick_ready=0;assert(ABoxAdc_Open(1,&c)==ABOX_HAL_INVALID);tick_ready=1;
    calibration_timeout=1;assert(ABoxAdc_Open(1,&c)==ABOX_HAL_TIMEOUT && !clocks[1]);calibration_timeout=0;
    assert(ABoxAdc_Open(1,&c)==ABOX_HAL_OK && clocks[1] && shared_divider==6U);
    assert(ABoxAdc_Open(2,&mismatch)==ABOX_HAL_BUSY);
    assert(ABoxAdc_Open(2,&c)==ABOX_HAL_OK && clocks[2]);
    {
      ABoxAdcWatchConfig w={100U,3000U,1U,1U,1U,3U,1U};
      ABoxAdcWatchEvents e={(void *)1,watchdog};uint8_t pending=7U;
      w.high=99U;assert(ABoxAdc_WatchConfigure(1U,&w,&e)==ABOX_HAL_INVALID);w.high=3000U;
      w.channel=16U;assert(ABoxAdc_WatchConfigure(2U,&w,&e)==ABOX_HAL_INVALID);w.channel=3U;
      assert(ABoxAdc_WatchConfigure(1U,&w,&e)==ABOX_HAL_OK && ABoxAdc_Close(1U)==ABOX_HAL_BUSY);
      assert(ABoxAdc_WatchConfigure(1U,&w,&e)==ABOX_HAL_BUSY);
#ifdef TEST_STM32
      assert(fake_adc_st[0].LTR==100U && fake_adc_st[0].HTR==3000U &&
             (fake_adc_st[0].CR1 & 0xc0025fU)==0xc00243U);
#else
      assert(fake_adc_low[0]==100U && fake_adc_high[0]==3000U &&
             (fake_adc_ctl0[0] & 0xc0025fU)==0xc00243U);
#endif
      assert(ABoxAdc_WatchRead(1U,&pending)==ABOX_HAL_OK && !pending);
      watchdog_flags[0]=1U;assert(ABoxAdc_WatchRead(1U,&pending)==ABOX_HAL_OK && pending);
      ABoxAdc_Irq(1U);assert(watchdog_callbacks==1U && !watchdog_flags[0]);
      watchdog_flags[0]=1U;assert(ABoxAdc_WatchClear(1U)==ABOX_HAL_OK && !watchdog_flags[0]);
      assert(ABoxAdcHw_DmaReserve(1U)==ABOX_HAL_OK);
      assert(ABoxAdc_WatchDisable(1U)==ABOX_HAL_OK && ABoxAdcHw_DmaRelease(1U,0)==ABOX_HAL_OK);
      assert(ABoxAdc_WatchRead(1U,&pending)==ABOX_HAL_INVALID);
      w.regular=0U;w.injected=0U;assert(ABoxAdc_WatchConfigure(2U,&w,0)==ABOX_HAL_INVALID);
      w.regular=1U;w.single_channel=0U;w.interrupt=0U;
      assert(ABoxAdc_WatchConfigure(2U,&w,0)==ABOX_HAL_OK);
      watchdog_flags[1]=1U;ABoxAdc_Irq(2U);
      assert(ABoxAdc_WatchRead(2U,&pending)==ABOX_HAL_OK && pending && watchdog_callbacks==1U);
      assert(ABoxAdc_WatchClear(2U)==ABOX_HAL_OK && ABoxAdc_WatchDisable(2U)==ABOX_HAL_OK);
    }
    {
      ABoxAdcDualSample both={0xaaaaU,0xbbbbU};
      assert(ABoxAdc_ReadDual(1U,16U,ABOX_ADC_SAMPLE_239_5,5U,&both)==ABOX_HAL_INVALID);
      assert(ABoxAdc_ReadDual(3U,3U,ABOX_ADC_SAMPLE_7_5,5U,&both)==ABOX_HAL_INVALID);
      assert(ABoxAdc_ReadDual(3U,5U,ABOX_ADC_SAMPLE_28_5,5U,&both)==ABOX_HAL_OK &&
             both.adc1==0x123U && both.adc2==0x234U && !dual_mode);
      both.adc1=0xaaaaU;both.adc2=0xbbbbU;conversion_timeout=1U;
      assert(ABoxAdc_ReadDual(3U,5U,ABOX_ADC_SAMPLE_28_5,3U,&both)==ABOX_HAL_TIMEOUT &&
             both.adc1==0xaaaaU && both.adc2==0xbbbbU && !dual_mode);
      conversion_timeout=0U;assert(ABoxAdc_ReadDual(3U,5U,ABOX_ADC_SAMPLE_28_5,3U,&both)==ABOX_HAL_ERROR);
      assert(ABoxAdc_Close(1U)==ABOX_HAL_OK && ABoxAdc_Close(2U)==ABOX_HAL_OK);
      assert(ABoxAdc_Open(1U,&c)==ABOX_HAL_OK && ABoxAdc_Open(2U,&c)==ABOX_HAL_OK);
    }
    {
      ABoxAdcSequenceEntry entries[]={{3U,ABOX_ADC_SAMPLE_7_5},{5U,ABOX_ADC_SAMPLE_28_5}};
      ABoxAdcInjectedConfig inj={entries,2U,ABOX_ADC_INJ_SOFTWARE};
      uint8_t ready=9U;uint16_t samples[]={0xdeadU,0xbeefU};
      inj.entry_count=5U;assert(ABoxAdc_StartInjected(1U,&inj)==ABOX_HAL_INVALID);inj.entry_count=2U;
      assert(ABoxAdc_StartInjected(1U,&inj)==ABOX_HAL_OK && injected_entries==2U && injected_software);
      assert(selected_trigger==(7U<<12U));
      assert(ABoxAdc_Close(1U)==ABOX_HAL_BUSY && ABoxAdcHw_DmaReserve(1U)==ABOX_HAL_BUSY);
      assert(ABoxAdc_Read(1U,3U,ABOX_ADC_SAMPLE_7_5,10U,&value)==ABOX_HAL_BUSY);
      conversion_timeout=1U;assert(ABoxAdc_InjectedReady(1U,&ready)==ABOX_HAL_OK && !ready);
      conversion_timeout=0U;assert(ABoxAdc_InjectedReady(1U,&ready)==ABOX_HAL_OK && ready);
      assert(ABoxAdc_FinishInjected(1U,5U,samples,1U)==ABOX_HAL_INVALID && samples[0]==0xdeadU);
      assert(ABoxAdc_FinishInjected(1U,5U,samples,2U)==ABOX_HAL_OK && samples[0]==0x111U && samples[1]==0x112U);
      inj.trigger=ABOX_ADC_INJ_TIM4_TRGO;injected_software=0U;
      assert(ABoxAdc_StartInjected(2U,&inj)==ABOX_HAL_OK && selected_trigger==(5U<<12U) && !injected_software);
      conversion_timeout=1U;samples[0]=0xdeadU;
      assert(ABoxAdc_FinishInjected(2U,3U,samples,2U)==ABOX_HAL_TIMEOUT && samples[0]==0xdeadU);
      conversion_timeout=0U;assert(ABoxAdc_StartInjected(2U,&inj)==ABOX_HAL_ERROR);
      assert(ABoxAdc_Close(2U)==ABOX_HAL_OK && ABoxAdc_Open(2U,&c)==ABOX_HAL_OK);
      assert(ABoxAdc_StartInjected(2U,&inj)==ABOX_HAL_OK && ABoxAdc_AbortInjected(2U)==ABOX_HAL_OK);
      assert(ABoxAdc_Read(2U,3U,ABOX_ADC_SAMPLE_7_5,10U,&value)==ABOX_HAL_ERROR);
      assert(ABoxAdc_Close(2U)==ABOX_HAL_OK && ABoxAdc_Open(2U,&c)==ABOX_HAL_OK);
    }
    {
      ABoxAdcDualEntry entries[]={{3U,4U,ABOX_ADC_SAMPLE_7_5},{5U,6U,ABOX_ADC_SAMPLE_28_5}};
      ABoxAdcDualInjectedConfig inj={entries,2U,ABOX_ADC_INJ_SOFTWARE};
      ABoxAdcDualSample samples[]={{0xdeadU,0xbeefU},{0xdeadU,0xbeefU}};uint8_t ready=0U;
      entries[0].adc2_channel=3U;assert(ABoxAdc_StartDualInjected(&inj)==ABOX_HAL_INVALID);entries[0].adc2_channel=4U;
      injected_entries=0U;assert(ABoxAdc_StartDualInjected(&inj)==ABOX_HAL_OK && dual_mode==(5U<<16U) && injected_entries==4U);
      assert(ABoxAdc_Close(1U)==ABOX_HAL_BUSY && ABoxAdc_Read(2U,3U,ABOX_ADC_SAMPLE_7_5,5U,&value)==ABOX_HAL_BUSY);
      conversion_timeout=1U;assert(ABoxAdc_DualInjectedReady(&ready)==ABOX_HAL_OK && !ready);
      conversion_timeout=0U;assert(ABoxAdc_DualInjectedReady(&ready)==ABOX_HAL_OK && ready);
      assert(ABoxAdc_FinishDualInjected(5U,samples,1U)==ABOX_HAL_INVALID && samples[0].adc1==0xdeadU);
      assert(ABoxAdc_FinishDualInjected(5U,samples,2U)==ABOX_HAL_OK && samples[0].adc1==0x111U && samples[0].adc2==0x111U && samples[1].adc1==0x112U && !dual_mode);
      inj.mode=ABOX_ADC_DUAL_INJECT_ALTERNATE;entries[0].adc2_channel=3U;
      assert(ABoxAdc_StartDualInjected(&inj)==ABOX_HAL_OK && dual_mode==(9U<<16U));
      assert(ABoxAdc_DualInjectedReady(&ready)==ABOX_HAL_OK && !ready);
      conversion_timeout=1U;assert(ABoxAdc_AdvanceDualInjected()==ABOX_HAL_BUSY);
      conversion_timeout=0U;assert(ABoxAdc_AdvanceDualInjected()==ABOX_HAL_OK && ABoxAdc_AdvanceDualInjected()==ABOX_HAL_BUSY);
      assert(ABoxAdc_FinishDualInjected(5U,samples,2U)==ABOX_HAL_OK && !dual_mode);
      inj.mode=ABOX_ADC_DUAL_INJECT_SIMULTANEOUS;entries[0].adc2_channel=4U;
      inj.trigger=ABOX_ADC_INJ_TIM4_TRGO;assert(ABoxAdc_StartDualInjected(&inj)==ABOX_HAL_OK && selected_trigger==(5U<<12U));
      conversion_timeout=1U;samples[0].adc1=0xdeadU;
      assert(ABoxAdc_FinishDualInjected(3U,samples,2U)==ABOX_HAL_TIMEOUT && samples[0].adc1==0xdeadU && !dual_mode);
      conversion_timeout=0U;assert(ABoxAdc_StartDualInjected(&inj)==ABOX_HAL_ERROR);
      assert(ABoxAdc_Close(1U)==ABOX_HAL_OK && ABoxAdc_Close(2U)==ABOX_HAL_OK);
      assert(ABoxAdc_Open(1U,&c)==ABOX_HAL_OK && ABoxAdc_Open(2U,&c)==ABOX_HAL_OK);
      assert(ABoxAdc_StartDualInjected(&inj)==ABOX_HAL_OK && ABoxAdc_AbortDualInjected()==ABOX_HAL_OK);
      assert(ABoxAdc_Close(1U)==ABOX_HAL_OK && ABoxAdc_Close(2U)==ABOX_HAL_OK);
      assert(ABoxAdc_Open(1U,&c)==ABOX_HAL_OK && ABoxAdc_Open(2U,&c)==ABOX_HAL_OK);
    }
    {
      ABoxAdcSequenceEntry entries[]={{3U,ABOX_ADC_SAMPLE_7_5},{5U,ABOX_ADC_SAMPLE_28_5}};
      ABoxAdcDmaConfig dma={entries,2U,ABOX_ADC_TRIG_TIM3_TRGO,0U,1U,0};
      assert(ABoxAdcHw_DmaReserve(2U)==ABOX_HAL_UNSUPPORTED);
      assert(ABoxAdcHw_DmaReserve(1U)==ABOX_HAL_OK);
      assert(ABoxAdc_Close(1U)==ABOX_HAL_BUSY && ABoxAdc_Read(1U,5U,ABOX_ADC_SAMPLE_28_5,10U,&value)==ABOX_HAL_BUSY);
      assert(ABoxAdcHw_DmaPrepare(1U,&dma)==ABOX_HAL_OK && selected_channel==5U);
      assert(ABoxAdcHw_DmaAddress(1U)!=0U && ABoxAdcHw_DmaStart(1U,&dma)==ABOX_HAL_OK);
      ABoxAdcHw_DmaPause(1U);assert(ABoxAdcHw_DmaRelease(1U,0)==ABOX_HAL_OK);
      {
        ABoxAdcLinkedConfig linked={entries,2U,ABOX_ADC_INJECT_AFTER_REGULAR,ABOX_ADC_INJ_SOFTWARE,0};
        uint16_t values[2];dma.linked=&linked;
        assert(ABoxAdcHw_DmaReserve(1U)==ABOX_HAL_OK && ABoxAdcHw_DmaPrepare(1U,&dma)==ABOX_HAL_OK && selected_auto);
        assert(ABoxAdcHw_DmaStart(1U,&dma)==ABOX_HAL_OK);
        assert(ABoxAdcHw_DmaInjectedFetch(1U,2U,values) && values[0]==0x111U && values[1]==0x112U);
        ABoxAdcHw_DmaInjectedMask(1U);assert(!ABoxAdcHw_DmaInjectedFetch(1U,2U,values));
        assert(ABoxAdcHw_DmaRelease(1U,0)==ABOX_HAL_OK);
#ifdef TEST_STM32
        assert(!(ADC1->CR1 & (ADC_CR1_JAUTO|ADC_CR1_JEOCIE)));
#else
        assert(!selected_auto && !(ADC_CTL0(ADC0)&ADC_CTL0_EOICIE));
#endif
        linked.mode=ABOX_ADC_INJECT_TRIGGERED;linked.trigger=ABOX_ADC_INJ_TIM4_TRGO;
        assert(ABoxAdcHw_DmaReserve(1U)==ABOX_HAL_OK && ABoxAdcHw_DmaPrepare(1U,&dma)==ABOX_HAL_OK && !selected_auto);
        assert(selected_trigger==(5U<<12U) && ABoxAdcHw_DmaStart(1U,&dma)==ABOX_HAL_OK);
#ifdef TEST_STM32
        assert(ADC1->CR2 & ADC_CR2_JEXTTRIG);
#else
        assert(injected_trigger_enabled);
#endif
        assert(ABoxAdcHw_DmaRelease(1U,0)==ABOX_HAL_OK);
        linked.trigger=ABOX_ADC_INJ_SOFTWARE;injected_software=0U;
        assert(ABoxAdcHw_DmaReserve(1U)==ABOX_HAL_OK && ABoxAdcHw_DmaPrepare(1U,&dma)==ABOX_HAL_OK);
        assert(ABoxAdcHw_DmaStart(1U,&dma)==ABOX_HAL_OK);
#ifdef TEST_STM32
        assert(!(ADC1->CR2 & ADC_CR2_JEXTTRIG));
        assert(ABoxAdcHw_DmaTriggerInjected(1U)==ABOX_HAL_OK && (ADC1->CR2 & ADC_CR2_JSWSTART));
#else
        assert(!injected_trigger_enabled);
        assert(ABoxAdcHw_DmaTriggerInjected(1U)==ABOX_HAL_OK && injected_software && injected_trigger_enabled);
#endif
        assert(ABoxAdcHw_DmaRelease(1U,0)==ABOX_HAL_OK);
      }
    }
    {
      ABoxAdcDualEntry entries[]={{3U,4U,ABOX_ADC_SAMPLE_7_5},{5U,6U,ABOX_ADC_SAMPLE_28_5}};
      ABoxAdcDualDmaConfig dma={entries,2U,ABOX_ADC_TRIG_TIM3_TRGO,0U,1U,ABOX_ADC_DUAL_SIMULTANEOUS,0};
      assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdc_Close(2U)==ABOX_HAL_BUSY);
      assert(ABoxAdcHw_DmaReserve(1U)==ABOX_HAL_BUSY && ABoxAdc_Read(2U,3U,ABOX_ADC_SAMPLE_7_5,5U,&value)==ABOX_HAL_BUSY);
      assert(ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && dual_mode==(6U<<16U));
      assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK);ABoxAdcHw_DualPause();
      assert(ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK && !dual_mode);
      {
        ABoxAdcDualEntry injected[]={{7U,8U,ABOX_ADC_SAMPLE_28_5}};
        ABoxAdcDualLinkedConfig linked={injected,1U,ABOX_ADC_INJ_TIM4_TRGO,0};ABoxAdcDualSample sample;
        dma.linked=&linked;
        assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && dual_mode==(1U<<16U));
        assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK && ABoxAdcHw_DualInjectedFetch(1U,&sample));
        assert(sample.adc1==0x111U && sample.adc2==0x111U && ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK);
        linked.mode=ABOX_ADC_DUAL_INJECT_ALTERNATE;
        assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && dual_mode==(2U<<16U));
        assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK);
#ifdef TEST_STM32
        assert(ADC2->CR1 & ADC_CR1_JEOCIE);
#else
        assert(ADC_CTL0(ADC1) & ADC_CTL0_EOICIE);
#endif
        assert(ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK);
        linked.mode=ABOX_ADC_DUAL_INJECT_SIMULTANEOUS;
        linked.automatic=1U;linked.trigger=ABOX_ADC_INJ_SOFTWARE;
        assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && selected_auto);
        assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK);
#ifdef TEST_STM32
        assert((ADC1->CR1 & ADC_CR1_JAUTO) && (ADC2->CR1 & ADC_CR1_JAUTO));
        assert(!(ADC1->CR2 & ADC_CR2_JEXTTRIG) && !(ADC2->CR2 & ADC_CR2_JEXTTRIG));
        ADC1->CR2|=ADC_CR2_ADON;ADC2->CR2|=ADC_CR2_ADON;
        ABoxAdcHw_DualRegularPause();assert((ADC1->CR2 & ADC_CR2_ADON) && (ADC2->CR2 & ADC_CR2_ADON));
#else
        ABoxAdcHw_DualRegularPause();assert(adc_enabled[0] && adc_enabled[1]);
#endif
        assert(ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK);
        linked.automatic=0U;linked.trigger=ABOX_ADC_INJ_TIM4_TRGO;
        dma.entry_count=1U;dma.mode=ABOX_ADC_DUAL_INTERLEAVED_FAST;dma.continuous=1U;
        entries[0].adc2_channel=entries[0].adc1_channel;entries[0].sample=ABOX_ADC_SAMPLE_1_5;
        assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && dual_mode==(3U<<16U));
        assert(selected_scan[0] && selected_scan[1]);
        assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK && ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK);
        dma.mode=ABOX_ADC_DUAL_INTERLEAVED_SLOW;dma.continuous=0U;
        assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && dual_mode==(4U<<16U));
        assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK && ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK);
        dma.linked=0;
      }
      dma.entry_count=1U;dma.mode=ABOX_ADC_DUAL_INTERLEAVED_FAST;dma.continuous=1U;
      entries[0].adc2_channel=entries[0].adc1_channel;entries[0].sample=ABOX_ADC_SAMPLE_1_5;
      assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && dual_mode==(7U<<16U));
      assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK && ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK && !dual_mode);
      dma.mode=ABOX_ADC_DUAL_INTERLEAVED_SLOW;dma.continuous=0U;entries[0].sample=ABOX_ADC_SAMPLE_13_5;
      assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualPrepare(&dma)==ABOX_HAL_OK && dual_mode==(8U<<16U));
      assert(ABoxAdcHw_DualStart(&dma)==ABOX_HAL_OK);
#ifdef TEST_STM32
      ADC1->CR2|=ADC_CR2_ADON;ADC2->CR2|=ADC_CR2_ADON;
      ABoxAdcHw_DualPause();assert(!(ADC1->CR2&ADC_CR2_ADON) && !(ADC2->CR2&ADC_CR2_ADON));
#else
      assert(adc_enabled[0] && adc_enabled[1]);ABoxAdcHw_DualPause();assert(!adc_enabled[0] && !adc_enabled[1]);
#endif
      assert(ABoxAdcHw_DualRelease(0)==ABOX_HAL_OK && !dual_mode);
      assert(ABoxAdcHw_DualReserve()==ABOX_HAL_OK && ABoxAdcHw_DualRelease(1)==ABOX_HAL_OK);
      assert(ABoxAdc_Read(2U,3U,ABOX_ADC_SAMPLE_7_5,5U,&value)==ABOX_HAL_ERROR);
      assert(ABoxAdc_Close(1U)==ABOX_HAL_OK && ABoxAdc_Close(2U)==ABOX_HAL_OK);
      assert(ABoxAdc_Open(1U,&c)==ABOX_HAL_OK && ABoxAdc_Open(2U,&c)==ABOX_HAL_OK);
    }
    assert(ABoxAdc_Read(2,16,ABOX_ADC_SAMPLE_239_5,10,&value)==ABOX_HAL_INVALID && value==0xdeadU);
    assert(ABoxAdc_Read(1,16,ABOX_ADC_SAMPLE_7_5,10,&value)==ABOX_HAL_INVALID && value==0xdeadU);
    assert(ABoxAdc_Read(1,5,ABOX_ADC_SAMPLE_28_5,10,&value)==ABOX_HAL_OK && value==0x0a5bU);
    assert(selected_channel==5U && selected_sample==ABOX_ADC_SAMPLE_28_5);
    assert(ABoxAdc_Read(1,17,ABOX_ADC_SAMPLE_239_5,10,&value)==ABOX_HAL_OK && selected_channel==17U);
    conversion_timeout=1;value=0xdeadU;
    assert(ABoxAdc_Read(1,5,ABOX_ADC_SAMPLE_28_5,3,&value)==ABOX_HAL_TIMEOUT && value==0xdeadU);
    conversion_timeout=0;assert(ABoxAdc_Read(1,5,ABOX_ADC_SAMPLE_28_5,3,&value)==ABOX_HAL_ERROR);
    assert(ABoxAdc_Close(1)==ABOX_HAL_OK);
    assert(ABoxAdc_Open(1,&mismatch)==ABOX_HAL_BUSY);
    assert(ABoxAdc_Close(2)==ABOX_HAL_OK && !clocks[1] && !clocks[2]);
    assert(ABoxAdc_Open(1,&mismatch)==ABOX_HAL_OK && shared_divider==8U);
    assert(ABoxAdc_Close(1)==ABOX_HAL_OK);
    c.clock_divider=12U;
#ifdef TEST_STM32
    assert(ABoxAdc_Open(1,&c)==ABOX_HAL_INVALID);
#else
    adc_pclk=108000000U;
    assert(ABoxAdc_Open(1,&c)==ABOX_HAL_OK && shared_divider==12U);
    assert(ABoxAdc_Close(1)==ABOX_HAL_OK);c.clock_divider=16U;
    assert(ABoxAdc_Open(1,&c)==ABOX_HAL_OK && shared_divider==16U);
    assert(ABoxAdc_Close(1)==ABOX_HAL_OK);
#endif
    adc_pclk=71000001U;c.clock_divider=6U;
    assert(ABoxAdc_Open(1,&c)==ABOX_HAL_OK && ABoxAdc_Close(1)==ABOX_HAL_OK);
    return 0;
}
