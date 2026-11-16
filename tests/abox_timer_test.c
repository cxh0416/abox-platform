#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_timer.h"
#include "abox_capture_dma_backend.h"
#include "abox_pwm_dma_backend.h"
#include "abox_timer_burst_backend.h"

static unsigned clocks[8], calls, last_id, output_mode, output_pulse, output_polarity, configured_period;
static unsigned capture_calls, capture_value, capture_overrun, capture_filter, capture_divider, capture_input, encoder_mode;
void fake_timer_clock(unsigned id,unsigned on) { clocks[id]=on; }
static void update(void *ctx,unsigned id) { assert(ctx==(void *)0x1234);++calls;last_id=id; }
static void capture(void *ctx,unsigned id,unsigned ch,uint32_t value,uint8_t overrun)
{ assert(ctx==(void *)0x5678 && id==2U && ch==2U);++capture_calls;capture_value=value;capture_overrun=overrun; }
static unsigned compare_calls;
static unsigned break_calls;
static unsigned reset_on,reset_off;
void fake_timer_reset(unsigned id,unsigned on) { assert(id>=1U && id<=7U);if(on)++reset_on;else ++reset_off; }
static void compared(void *ctx,unsigned id,unsigned ch)
{ assert(ctx==(void *)0x9876 && id==2U && ch==3U);++compare_calls; }
static void break_detected(void *ctx,unsigned id)
{ assert(ctx==(void *)0x2468 && id==1U);++break_calls; }

#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
TIM_TypeDef fake_timer_st[7]={{.id=1},{.id=2},{.id=3},{.id=4},{.id=5},{.id=6},{.id=7}};
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h) { configured_period=h->Init.Period+1U;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_DeInit(TIM_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *h) { h->Instance->irq|=1U;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *h) { h->Instance->irq&=~1U;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_OC_ConfigChannel(TIM_HandleTypeDef *h,const TIM_OC_InitTypeDef *c,uint32_t ch)
{ (void)h;(void)ch;output_mode=c->OCMode;output_pulse=c->Pulse;output_polarity=c->OCPolarity;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_IC_ConfigChannel(TIM_HandleTypeDef *h,const TIM_IC_InitTypeDef *c,uint32_t channel)
{ (void)h;(void)channel;capture_filter=c->ICFilter;capture_divider=c->ICPrescaler;capture_input=c->ICSelection;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Encoder_Init(TIM_HandleTypeDef *h,const TIM_Encoder_InitTypeDef *c)
{ h->Instance->SMCR=c->EncoderMode;encoder_mode=c->EncoderMode;capture_filter=c->IC2Filter;return HAL_OK; }
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *h,uint32_t ch)
{ unsigned n=ch/4U;h->Instance->flags &= ~(TIM_FLAG_CC1<<n);return h->Instance->capture[n]; }
void TIM_CCxChannelCmd(TIM_TypeDef *t,uint32_t channel,uint32_t enabled)
{ if(enabled)t->enabled|=1U<<(channel/4U);else t->enabled&=~(1U<<(channel/4U)); }
static void raise_update(unsigned id) { fake_timer_st[id-1U].flags|=1U; }
static unsigned main_output(void) { return !!(fake_timer_st[0].BDTR & TIM_BDTR_MOE); }
static uint32_t break_config(void) { return fake_timer_st[0].BDTR; }
static uint32_t advanced_idle(void) { return fake_timer_st[0].CR2; }
static uint32_t advanced_channels(void) { return fake_timer_st[0].CCER; }
static void force_break(void) { fake_timer_st[0].flags|=TIM_FLAG_BREAK;fake_timer_st[0].BDTR &= ~TIM_BDTR_MOE; }
static uint32_t slave_register(unsigned id) { return fake_timer_st[id-1U].SMCR; }
static uint32_t one_pulse_register(unsigned id) { return fake_timer_st[id-1U].CR1; }
static uint32_t repetition_register(void) { return fake_timer_st[0].RCR; }
static uint32_t software_update(void) { return fake_timer_st[0].EGR; }
static uint32_t *burst_config(void) { return &fake_timer_st[0].DCR; }
static uint32_t hall_register(unsigned id) { return fake_timer_st[id-1U].CR2; }
static void set_hall_raw(unsigned id) { fake_timer_st[id-1U].CR2 |= TIM_CR2_TI1S; }
static void raise_capture(unsigned id,unsigned ch,uint32_t value,unsigned lost)
{ fake_timer_st[id-1U].capture[ch-1U]=value;fake_timer_st[id-1U].flags|=TIM_FLAG_CC1<<(ch-1U);if(lost)fake_timer_st[id-1U].flags|=TIM_FLAG_CC1OF<<(ch-1U); }
static unsigned active_channels(unsigned id) { return fake_timer_st[id-1U].enabled; }
static unsigned pwm_dma_enabled(unsigned id) { return fake_timer_st[id-1U].DIER & TIM_DMA_UPDATE; }
static uint32_t dma_requests(unsigned id) { return fake_timer_st[id-1U].DIER; }
static unsigned pwm_preload_enabled(unsigned id,unsigned ch) { return fake_timer_st[id-1U].preload & (1U<<(ch-1U)); }
#else
#include "gd32f10x.h"
static unsigned flags[8],irq[8],counter[8],enabled_channels[8],captured[8][4];
uint32_t fake_timer_ctl0[8],fake_timer_ctl1[8],fake_timer_chctl2[8],fake_timer_cchp[8],fake_timer_smcfg[8];
uint32_t fake_timer_crep[8],fake_timer_swevg[8];
uint32_t fake_timer_dmacfg[8],fake_timer_dmatb[8];
uint32_t fake_timer_dma_capture[8][4];
static uint32_t dma_enabled[8], preload[8];
static uint32_t dma_requests(unsigned id) { return dma_enabled[id]; }
void rcu_periph_clock_enable(rcu_periph_enum id) { fake_timer_clock(id,1); }
void rcu_periph_clock_disable(rcu_periph_enum id) { fake_timer_clock(id,0); }
void rcu_periph_reset_enable(rcu_periph_reset_enum id) { fake_timer_reset(id,1U); }
void rcu_periph_reset_disable(rcu_periph_reset_enum id) { fake_timer_reset(id,0U); }
void timer_deinit(uint32_t id) { (void)id; }
void timer_struct_para_init(timer_parameter_struct *p) { memset(p,0,sizeof(*p)); }
void timer_init(uint32_t id,timer_parameter_struct *p) { (void)id;configured_period=p->period+1U; }
void timer_flag_clear(uint32_t id,uint32_t flag) { flags[id]&=~flag; }
FlagStatus timer_flag_get(uint32_t id,uint32_t flag) { return (flags[id]&flag)?SET:RESET; }
void timer_channel_input_struct_para_init(timer_ic_parameter_struct *p) { memset(p,0,sizeof(*p)); }
void timer_input_capture_config(uint32_t id,uint16_t ch,timer_ic_parameter_struct *p)
{ capture_filter=p->icfilter;capture_divider=p->icprescaler;capture_input=p->icselection;enabled_channels[id]|=1U<<ch; }
uint32_t timer_channel_capture_value_register_read(uint32_t id,uint16_t ch)
{ flags[id]&=~(TIMER_FLAG_CH0<<ch);return captured[id][ch]; }
void timer_quadrature_decoder_mode_config(uint32_t id,uint32_t mode,uint16_t a,uint16_t b)
{ (void)id;(void)a;(void)b;encoder_mode=mode; }
void timer_internal_clock_config(uint32_t id) { (void)id;encoder_mode=0; }
void timer_interrupt_enable(uint32_t id,uint32_t flag) { irq[id]|=flag; }
void timer_interrupt_disable(uint32_t id,uint32_t flag) { irq[id]&=~flag; }
void timer_dma_enable(uint32_t id,uint16_t flag) { dma_enabled[id]|=flag; }
void timer_dma_disable(uint32_t id,uint16_t flag) { dma_enabled[id]&=~flag; }
void timer_master_output_trigger_source_select(uint32_t id,uint32_t value) { (void)id;assert(value<=112U); }
void timer_enable(uint32_t id) { (void)id; }
void timer_disable(uint32_t id) { (void)id; }
uint32_t timer_counter_read(uint32_t id) { return counter[id]; }
void timer_counter_value_config(uint32_t id,uint32_t value) { counter[id]=value; }
FlagStatus timer_interrupt_flag_get(uint32_t id,uint32_t flag) { return (flags[id]&irq[id]&flag)?SET:RESET; }
void timer_interrupt_flag_clear(uint32_t id,uint32_t flag) { flags[id]&=~flag; }
void timer_channel_output_struct_para_init(timer_oc_parameter_struct *p) { memset(p,0,sizeof(*p)); }
void timer_channel_output_config(uint32_t id,uint16_t ch,timer_oc_parameter_struct *p)
{ (void)id;(void)ch;output_polarity=p->ocpolarity; }
void timer_channel_output_mode_config(uint32_t id,uint16_t ch,uint16_t mode) { (void)id;(void)ch;output_mode=mode; }
void timer_channel_output_pulse_value_config(uint32_t id,uint16_t ch,uint16_t pulse) { (void)id;(void)ch;output_pulse=pulse; }
void timer_channel_output_shadow_config(uint32_t id,uint16_t ch,uint16_t shadow)
{ if(shadow)preload[id]|=1U<<ch;else preload[id]&=~(1U<<ch); }
void timer_channel_output_state_config(uint32_t id,uint16_t ch,uint32_t state)
{ if(state)enabled_channels[id]|=1U<<ch;else enabled_channels[id]&=~(1U<<ch); }
void timer_primary_output_config(uint32_t id,ControlStatus state)
{ if(state)fake_timer_cchp[id]|=TIMER_CCHP_POEN;else fake_timer_cchp[id]&=~TIMER_CCHP_POEN; }
static void raise_update(unsigned id) { flags[id]|=1U; }
static unsigned main_output(void) { return !!(fake_timer_cchp[1] & TIMER_CCHP_POEN); }
static uint32_t break_config(void) { return fake_timer_cchp[1]; }
static uint32_t advanced_idle(void) { return fake_timer_ctl1[1]; }
static uint32_t advanced_channels(void) { return fake_timer_chctl2[1]; }
static void force_break(void) { flags[1]|=TIMER_FLAG_BRK;fake_timer_cchp[1]&=~TIMER_CCHP_POEN; }
static uint32_t slave_register(unsigned id) { return fake_timer_smcfg[id]; }
static uint32_t one_pulse_register(unsigned id) { return fake_timer_ctl0[id]; }
static uint32_t repetition_register(void) { return fake_timer_crep[1]; }
static uint32_t software_update(void) { return fake_timer_swevg[1]; }
static uint32_t *burst_config(void) { return &fake_timer_dmacfg[1]; }
static uint32_t hall_register(unsigned id) { return fake_timer_ctl1[id]; }
static void set_hall_raw(unsigned id) { fake_timer_ctl1[id] |= TIMER_CTL1_TI0S; }
static void raise_capture(unsigned id,unsigned ch,uint32_t value,unsigned lost)
{ captured[id][ch-1U]=value;flags[id]|=TIMER_FLAG_CH0<<(ch-1U);if(lost)flags[id]|=TIMER_FLAG_CH0O<<(ch-1U); }
static unsigned active_channels(unsigned id) { return enabled_channels[id]; }
static unsigned pwm_dma_enabled(unsigned id) { return dma_enabled[id] & TIMER_DMA_UPD; }
static unsigned pwm_preload_enabled(unsigned id,unsigned ch) { return preload[id] & (1U<<(ch-1U)); }
#endif

int main(void)
{
    ABoxTimerConfig c={72U,65536U,ABOX_TIMER_UP};
    ABoxTimerEvents e={(void *)0x1234,update};
    ABoxTimerBreakEvents be={(void *)0x2468,break_detected};
    uint32_t value=99U;
    assert(ABoxTimer_Open(0,&c)==ABOX_HAL_INVALID);
    c.period=0;assert(ABoxTimer_Open(1,&c)==ABOX_HAL_INVALID);c.period=65536U;
    assert(ABoxTimer_Open(1,&c)==ABOX_HAL_OK && clocks[1] && configured_period==65536U);
    assert(ABoxTimer_SetCaptureDmaOnUpdate(6U,1U)==ABOX_HAL_UNSUPPORTED);
    assert(ABoxTimer_SetCaptureDmaOnUpdate(1U,1U)==ABOX_HAL_OK && (hall_register(1U)&8U));
    assert(ABoxTimer_SetCaptureDmaOnUpdate(1U,0U)==ABOX_HAL_OK && !(hall_register(1U)&8U));
    assert(ABoxTimer_Reset(1U)==ABOX_HAL_BUSY && !reset_on);
    *burst_config()=0x456U;
    assert(ABoxTimerBurstHw_Reserve(1U)==ABOX_HAL_OK);
    assert(ABoxTimer_SetCaptureDmaOnUpdate(1U,1U)==ABOX_HAL_BUSY);
    assert(ABoxTimerBurstHw_Reserve(1U)==ABOX_HAL_BUSY && ABoxTimer_Stop(1U)==ABOX_HAL_BUSY);
    ABoxTimerBurstHw_Config(1U,13U,4U);
    assert(*burst_config()==0x30dU && ABoxTimerBurstHw_Address(1U)!=0U);
    ABoxTimerBurstHw_Request(1U,ABOX_TIMER_BURST_CC2,1);assert(dma_requests(1U)&(1U<<10));
    ABoxTimerBurstHw_Request(1U,ABOX_TIMER_BURST_TRIGGER,1);assert(dma_requests(1U)&(1U<<14));
    ABoxTimerBurstHw_Request(1U,ABOX_TIMER_BURST_COM,1);assert(dma_requests(1U)&(1U<<13));
    ABoxTimerBurstHw_Request(1U,ABOX_TIMER_BURST_CC2,0);
    ABoxTimerBurstHw_Request(1U,ABOX_TIMER_BURST_TRIGGER,0);
    ABoxTimerBurstHw_Request(1U,ABOX_TIMER_BURST_COM,0);
    assert(!(dma_requests(1U)&((1U<<10)|(1U<<13)|(1U<<14))));
    ABoxTimerBurstHw_Release(1U);assert(*burst_config()==0x456U);
    assert(ABoxTimer_SetRepetition(2U,3U)==ABOX_HAL_UNSUPPORTED);
    assert(ABoxTimer_SetRepetition(1U,255U)==ABOX_HAL_OK && repetition_register()==255U && software_update()==1U);
    assert(ABoxTimer_SetRepetition(1U,0U)==ABOX_HAL_OK && repetition_register()==0U);
    assert(ABoxTimer_SetOnePulse(0U,1U)==ABOX_HAL_INVALID);
    assert(ABoxTimer_SetOnePulse(1U,2U)==ABOX_HAL_INVALID);
    assert(ABoxTimer_SetOnePulse(1U,1U)==ABOX_HAL_OK && (one_pulse_register(1U)&8U));
    assert(ABoxTimer_SetOnePulse(1U,0U)==ABOX_HAL_OK && !(one_pulse_register(1U)&8U));
    {
        ABoxTimerBreakConfig bc={0x93U,1U,1U,0U,1U,1U};uint8_t state=9U;
        assert(ABoxTimer_ConfigureBreak(2U,&bc)==ABOX_HAL_UNSUPPORTED);
        bc.auto_rearm=2U;assert(ABoxTimer_ConfigureBreak(1U,&bc)==ABOX_HAL_INVALID);bc.auto_rearm=0U;
        assert(ABoxTimer_ConfigureBreak(1U,&bc)==ABOX_HAL_OK && break_config()==0x3c93U);
        assert(ABoxTimer_BreakPending(1U,&state)==ABOX_HAL_OK && !state);
        assert(ABoxTimer_SetBreakEvents(2U,&be)==ABOX_HAL_UNSUPPORTED);
        assert(ABoxTimer_SetBreakEvents(1U,&be)==ABOX_HAL_OK);
        force_break();ABoxTimer_Irq(1U);ABoxTimer_Irq(1U);
        assert(break_calls==1U && ABoxTimer_BreakPending(1U,&state)==ABOX_HAL_OK && state);
        assert(ABoxTimer_ClearBreak(1U)==ABOX_HAL_OK);
        assert(ABoxTimer_OutputsArmed(1U,&state)==ABOX_HAL_OK && !state);
        assert(ABoxTimer_RearmOutputs(1U)==ABOX_HAL_INVALID);
    }
    assert(ABoxTimer_SetTrigger(1U,ABOX_TIMER_TRGO_UPDATE)==ABOX_HAL_OK);
    assert(ABoxTimer_SetTrigger(1U,(ABoxTimerTrigger)8U)==ABOX_HAL_INVALID);
    assert(ABoxTimer_Open(1,&c)==ABOX_HAL_BUSY);
    assert(ABoxTimer_Read(1,&value)==ABOX_HAL_OK && value==0U);
    assert(ABoxTimer_SetCounter(1,65535U)==ABOX_HAL_OK);
    assert(ABoxTimer_Read(1,&value)==ABOX_HAL_OK && value==65535U);
    assert(ABoxTimer_SetCounter(1,65536U)==ABOX_HAL_INVALID);
    assert(ABoxPwm_Configure(1,1,65537U,0U)==ABOX_HAL_INVALID);
    assert(ABoxPwm_Configure(1,1,0U,0U)==ABOX_HAL_OK && output_mode==1U);
    assert(ABoxPwm_Configure(1,1,65536U,1U)==ABOX_HAL_OK && output_mode==2U && output_pulse==0U && output_polarity==1U);
    assert(ABoxPwm_Configure(1,1,123U,0U)==ABOX_HAL_OK && output_mode==3U && output_pulse==123U);
    assert(ABoxPwm_Configure(1,2,456U,0U)==ABOX_HAL_OK);
    assert(ABoxPwm_EnableComplementary(1U,1U)==ABOX_HAL_INVALID);
    assert(ABoxPwm_ConfigureAdvancedOutput(1U,1U,1U,1U,1U)==ABOX_HAL_OK);
    assert((advanced_idle() & 0x300U)==0x300U && (advanced_channels() & 8U));
    assert(ABoxPwm_ConfigureAdvancedOutput(1U,4U,0U,1U,0U)==ABOX_HAL_INVALID);
    {
        const uint16_t pulses[]={0U,100U,65535U};
        assert(ABoxPwmHw_DmaReserve(1U,1U,42U,pulses,3U)==ABOX_HAL_OK);
        assert(ABoxPwmHw_DmaReserve(1U,2U,42U,pulses,3U)==ABOX_HAL_BUSY);
        assert(ABoxPwm_Configure(1U,1U,5U,0U)==ABOX_HAL_BUSY && ABoxPwm_Release(1U,1U)==ABOX_HAL_BUSY);
        assert(ABoxTimer_Stop(1U)==ABOX_HAL_BUSY && ABoxTimer_Close(1U)==ABOX_HAL_BUSY);
        assert(ABoxPwmHw_DmaAddress(1U,1U)!=0U);
        assert(ABoxPwmHw_DmaPrepare(1U,1U,42U)==ABOX_HAL_OK && output_mode==3U && output_pulse==42U);
        assert((advanced_idle() & 0x300U)==0x300U && (advanced_channels() & 8U));
        assert(pwm_preload_enabled(1U,1U));
        ABoxPwmHw_DmaEnable(1U);assert(pwm_dma_enabled(1U));
        ABoxPwmHw_DmaPause(1U);assert(!pwm_dma_enabled(1U));
        ABoxPwmHw_DmaRelease(1U,1U);
        assert(ABoxPwm_Configure(1U,1U,123U,0U)==ABOX_HAL_OK && !pwm_preload_enabled(1U,1U));
    }
    assert(ABoxPwm_EnableComplementary(1U,1U)==ABOX_HAL_OK && (advanced_channels() & 4U));
    assert(ABoxPwm_Enable(1,1)==ABOX_HAL_OK && ABoxPwm_Enable(1,2)==ABOX_HAL_OK && main_output());
    assert(ABoxPwm_ConfigureAdvancedOutput(1U,1U,0U,0U,0U)==ABOX_HAL_BUSY);
    assert(ABoxPwm_Configure(1,1,5U,0U)==ABOX_HAL_BUSY);
    assert(ABoxTimer_Start(1,&e)==ABOX_HAL_OK);
    assert(ABoxTimer_SetRepetition(1U,3U)==ABOX_HAL_BUSY);
    assert(ABoxTimer_SetOnePulse(1U,1U)==ABOX_HAL_BUSY);
    assert(ABoxTimer_SetTrigger(1U,ABOX_TIMER_TRGO_OC1)==ABOX_HAL_BUSY);
    raise_update(1);ABoxTimer_Irq(1);assert(calls==1U && last_id==1U);ABoxTimer_Irq(1);assert(calls==1U);
    assert(ABoxTimer_Stop(1)==ABOX_HAL_BUSY);
    {
        uint8_t state=0U;force_break();
        ABoxTimer_Irq(1U);ABoxTimer_Irq(1U);assert(break_calls==2U);
        assert(ABoxTimer_BreakPending(1U,&state)==ABOX_HAL_OK && state);
        assert(ABoxTimer_OutputsArmed(1U,&state)==ABOX_HAL_OK && !state);
        assert(ABoxTimer_RearmOutputs(1U)==ABOX_HAL_BUSY);
        assert(ABoxTimer_ClearBreak(1U)==ABOX_HAL_OK && ABoxTimer_RearmOutputs(1U)==ABOX_HAL_OK);
        assert(ABoxTimer_OutputsArmed(1U,&state)==ABOX_HAL_OK && state);
    }
    assert(ABoxPwm_Disable(1,1)==ABOX_HAL_OK && main_output());
    assert(ABoxPwm_Disable(1,2)==ABOX_HAL_OK && main_output());
    assert(ABoxTimer_Stop(1)==ABOX_HAL_BUSY && ABoxPwm_Release(1,1)==ABOX_HAL_BUSY);
    assert(ABoxPwm_DisableComplementary(1U,1U)==ABOX_HAL_OK && !main_output());
    assert(ABoxTimer_Stop(1)==ABOX_HAL_OK && ABoxTimer_SetBreakEvents(1U,0)==ABOX_HAL_OK && ABoxTimer_Close(1)==ABOX_HAL_OK && !clocks[1]);
    assert(ABoxTimer_Reset(1U)==ABOX_HAL_OK && reset_on==1U && reset_off==1U);
    {
        const uint16_t valid[]={0U,50U,100U}, invalid[]={0U,101U};
        c.period=100U;
        assert(ABoxTimer_Open(5U,&c)==ABOX_HAL_OK && ABoxPwm_Configure(5U,4U,5U,1U)==ABOX_HAL_OK);
        assert(ABoxPwmHw_DmaReserve(5U,4U,101U,valid,3U)==ABOX_HAL_INVALID);
        assert(ABoxPwmHw_DmaReserve(5U,4U,0U,invalid,2U)==ABOX_HAL_INVALID);
        assert(ABoxPwmHw_DmaReserve(5U,4U,100U,valid,3U)==ABOX_HAL_OK);
        assert(ABoxPwmHw_DmaPrepare(5U,4U,100U)==ABOX_HAL_OK && output_polarity==1U);
        assert(ABoxPwm_Enable(5U,4U)==ABOX_HAL_OK && ABoxTimer_Start(5U,0)==ABOX_HAL_OK);
        assert(ABoxPwm_Disable(5U,4U)==ABOX_HAL_OK && ABoxTimer_Stop(5U)==ABOX_HAL_BUSY);
        ABoxPwmHw_DmaRelease(5U,4U);
        assert(ABoxTimer_Stop(5U)==ABOX_HAL_OK && ABoxPwm_Release(5U,4U)==ABOX_HAL_OK);
        assert(ABoxTimer_Close(5U)==ABOX_HAL_OK);
        c.period=65536U;
    }
    c.mode=ABOX_TIMER_DOWN;assert(ABoxTimer_Open(6,&c)==ABOX_HAL_INVALID);
    assert(ABoxTimer_Open(2,&c)==ABOX_HAL_OK && ABoxPwm_Configure(2,1,5U,0U)==ABOX_HAL_OK);
    assert(ABoxPwm_Release(2,1)==ABOX_HAL_OK);
    assert(ABoxTimer_Close(2)==ABOX_HAL_OK);
    c.mode=ABOX_TIMER_CENTER_BOTH;
    assert(ABoxTimer_Open(2U,&c)==ABOX_HAL_OK && ABoxPwm_Configure(2U,1U,5U,0U)==ABOX_HAL_OK);
    assert(ABoxPwm_Enable(2U,1U)==ABOX_HAL_OK && ABoxTimer_Start(2U,0)==ABOX_HAL_OK);
    assert(ABoxPwm_Disable(2U,1U)==ABOX_HAL_OK && ABoxTimer_Stop(2U)==ABOX_HAL_OK);
    assert(ABoxPwm_Release(2U,1U)==ABOX_HAL_OK && ABoxTimer_Close(2U)==ABOX_HAL_OK);
    c.mode=ABOX_TIMER_UP;assert(ABoxTimer_Open(6,&c)==ABOX_HAL_OK);
    assert(ABoxTimer_SetOnePulse(6U,1U)==ABOX_HAL_OK && (one_pulse_register(6U)&8U));
    assert(ABoxTimer_SetOnePulse(6U,0U)==ABOX_HAL_OK && !(one_pulse_register(6U)&8U));
    assert(ABoxPwm_Configure(6,1,5U,0U)==ABOX_HAL_INVALID && ABoxTimer_Close(6)==ABOX_HAL_OK);
    {
        ABoxCompareEvents ce={(void *)0x9876,compared};uint8_t matched=99U;
        assert(ABoxTimer_Open(2U,&c)==ABOX_HAL_OK);
        assert(ABoxCompare_Configure(2U,3U,65536U,ABOX_COMPARE_TOGGLE,0U)==ABOX_HAL_INVALID);
        assert(ABoxCompare_Configure(2U,3U,123U,ABOX_COMPARE_TOGGLE,1U)==ABOX_HAL_OK);
        assert(output_mode==7U && output_pulse==123U && output_polarity==1U);
        assert(ABoxPwm_Configure(2U,3U,1U,0U)==ABOX_HAL_BUSY);
        assert(ABoxPwm_Enable(2U,3U)==ABOX_HAL_BUSY);
        assert(ABoxCompare_Start(2U,3U,0)==ABOX_HAL_OK && ABoxTimer_Start(2U,0)==ABOX_HAL_OK);
        assert(ABoxCompare_Poll(2U,3U,&matched)==ABOX_HAL_OK && !matched);
        raise_capture(2U,3U,0U,0U);
        assert(ABoxCompare_Poll(2U,3U,&matched)==ABOX_HAL_OK && matched);
        assert(ABoxCompare_Poll(2U,3U,&matched)==ABOX_HAL_OK && !matched);
        assert(ABoxCompare_Release(2U,3U)==ABOX_HAL_BUSY && ABoxTimer_Stop(2U)==ABOX_HAL_BUSY);
        assert(ABoxCompare_Stop(2U,3U)==ABOX_HAL_OK);
        assert(ABoxCompare_Start(2U,3U,&ce)==ABOX_HAL_OK);
        assert(ABoxCompare_Poll(2U,3U,&matched)==ABOX_HAL_BUSY);
        raise_capture(2U,3U,0U,0U);ABoxTimer_Irq(2U);ABoxTimer_Irq(2U);assert(compare_calls==1U);
        assert(ABoxCompare_Stop(2U,3U)==ABOX_HAL_OK && ABoxTimer_Stop(2U)==ABOX_HAL_OK);
        assert(ABoxCompare_Release(2U,3U)==ABOX_HAL_OK && ABoxTimer_Close(2U)==ABOX_HAL_OK);
    }
    {
        ABoxTimerSlaveConfig sc={ABOX_TIMER_EXTERNAL_CLOCK_1,ABOX_TIMER_ETR,4U,9U,1U};
        ABoxEncoderConfig en={ABOX_ENCODER_BOTH,ABOX_CAPTURE_RISING,ABOX_CAPTURE_RISING,0U,0U};
        set_hall_raw(2U);assert(ABoxTimer_Open(2U,&c)==ABOX_HAL_OK && !(hall_register(2U)&0x80U));
        assert(ABoxTimer_SetHallInput(0U,1U)==ABOX_HAL_INVALID);
        assert(ABoxTimer_SetHallInput(6U,1U)==ABOX_HAL_UNSUPPORTED);
        assert(ABoxTimer_SetHallInput(2U,2U)==ABOX_HAL_INVALID);
        assert(ABoxTimer_SetSlave(6U,&sc)==ABOX_HAL_INVALID);
        assert(ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_OK && (slave_register(2U)&0xff77U)==0xa977U);
        assert(ABoxEncoder_Configure(2U,&en)==ABOX_HAL_BUSY);
        assert(ABoxTimer_Start(2U,0)==ABOX_HAL_OK && ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_BUSY);
        assert(ABoxTimer_Stop(2U)==ABOX_HAL_OK);
        sc.mode=ABOX_TIMER_SLAVE_GATED;sc.source=ABOX_TIMER_TI1_EDGE;
        assert(ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_INVALID);
        sc.mode=ABOX_TIMER_SLAVE_TRIGGER;sc.source=ABOX_TIMER_ITR1;sc.etr_prescaler=1U;sc.etr_filter=0U;sc.etr_inverted=0U;
        assert(ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_OK && (slave_register(2U)&7U)==0U);
        assert(ABoxTimer_Start(2U,0)==ABOX_HAL_OK && (slave_register(2U)&7U)==6U);
        assert(ABoxTimer_Stop(2U)==ABOX_HAL_OK && (slave_register(2U)&7U)==0U);
        sc.mode=ABOX_TIMER_EXTERNAL_CLOCK_2;sc.source=ABOX_TIMER_ETR;
        assert(ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_OK && (slave_register(2U)&0x4000U));
        sc.mode=ABOX_TIMER_SLAVE_DISABLED;assert(ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_OK);
        assert(ABoxTimer_SetHallInput(2U,1U)==ABOX_HAL_OK && (hall_register(2U)&0x80U));
        assert(ABoxEncoder_Configure(2U,&en)==ABOX_HAL_BUSY);
        {
            ABoxCaptureConfig ic={ABOX_CAPTURE_RISING,ABOX_CAPTURE_TRIGGER,1U,3U};uint8_t lost=0U;
            sc.mode=ABOX_TIMER_SLAVE_RESET;sc.source=ABOX_TIMER_TI1_EDGE;
            assert(ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_OK);
            assert(ABoxCapture_Configure(2U,1U,&ic)==ABOX_HAL_OK && ABoxCapture_Start(2U,1U,0)==ABOX_HAL_OK);
            assert(ABoxTimer_SetHallInput(2U,0U)==ABOX_HAL_BUSY);
            assert(ABoxTimer_Start(2U,0)==ABOX_HAL_OK);
            raise_capture(2U,1U,345U,0U);assert(ABoxCapture_Read(2U,1U,&value,&lost)==ABOX_HAL_OK && value==345U && !lost);
            assert(ABoxCapture_Stop(2U,1U)==ABOX_HAL_OK && ABoxTimer_Stop(2U)==ABOX_HAL_OK);
            assert(ABoxCapture_Release(2U,1U)==ABOX_HAL_OK);
            sc.mode=ABOX_TIMER_SLAVE_DISABLED;assert(ABoxTimer_SetSlave(2U,&sc)==ABOX_HAL_OK);
        }
        assert(ABoxTimer_SetHallInput(2U,0U)==ABOX_HAL_OK && !(hall_register(2U)&0x80U));
        assert(ABoxTimer_SetOnePulse(2U,1U)==ABOX_HAL_OK);
        assert(ABoxEncoder_Configure(2U,&en)==ABOX_HAL_BUSY);
        assert(ABoxTimer_SetOnePulse(2U,0U)==ABOX_HAL_OK);
        assert(ABoxEncoder_Configure(2U,&en)==ABOX_HAL_OK);
        assert(ABoxTimer_SetOnePulse(2U,1U)==ABOX_HAL_BUSY);
        assert(ABoxEncoder_Release(2U)==ABOX_HAL_OK);
        assert(ABoxTimer_Close(2U)==ABOX_HAL_OK);
    }
    {
        ABoxCaptureConfig ic={ABOX_CAPTURE_FALLING,ABOX_CAPTURE_INDIRECT,4U,9U};
        ABoxCaptureEvents ce={(void *)0x5678,capture};
        ABoxEncoderConfig en={ABOX_ENCODER_MODE2,ABOX_CAPTURE_RISING,ABOX_CAPTURE_FALLING,3U,7U};
        uint8_t lost=77U;
        assert(ABoxTimer_Open(2,&c)==ABOX_HAL_OK);
        assert(ABoxCapture_Configure(2,1,&ic)==ABOX_HAL_OK && capture_filter==9U && capture_divider==8U && capture_input==2U);
        assert(ABoxPwm_Configure(2,1,1U,0U)==ABOX_HAL_BUSY && ABoxEncoder_Configure(2,&en)==ABOX_HAL_BUSY);
        assert(ABoxCaptureHw_DmaReserve(2U,1U)==ABOX_HAL_OK);
        assert(ABoxCaptureHw_DmaAddress(2U,1U)!=0U);
        ABoxCaptureHw_DmaEnable(2U,1U);
        assert(ABoxCapture_Read(2U,1U,&value,&lost)==ABOX_HAL_BUSY);
        assert(ABoxCapture_Stop(2U,1U)==ABOX_HAL_BUSY);
        raise_capture(2U,1U,1U,1U);assert(ABoxCaptureHw_DmaOvercapture(2U,1U));
        ABoxCaptureHw_DmaRelease(2U,1U);
        assert(ABoxCapture_Start(2,1,0)==ABOX_HAL_OK && ABoxTimer_Start(2,0)==ABOX_HAL_OK);
        assert(ABoxCapture_Read(2,1,&value,&lost)==ABOX_HAL_BUSY);
        raise_capture(2,1,1234U,1U);assert(ABoxCapture_Read(2,1,&value,&lost)==ABOX_HAL_OK && value==1234U && lost==1U);
        assert(ABoxTimer_Stop(2)==ABOX_HAL_BUSY && ABoxCapture_Release(2,1)==ABOX_HAL_BUSY);
        assert(ABoxCapture_Stop(2,1)==ABOX_HAL_OK && ABoxCapture_Release(2,1)==ABOX_HAL_OK);
        assert(ABoxCapture_Configure(2,2,&ic)==ABOX_HAL_OK && ABoxCapture_Start(2,2,&ce)==ABOX_HAL_OK);
        assert(ABoxCapture_Read(2,2,&value,&lost)==ABOX_HAL_BUSY);
        raise_capture(2,2,5678U,1U);ABoxTimer_Irq(2);
        assert(capture_calls==1U && capture_value==5678U && capture_overrun==1U);
        ABoxTimer_Irq(2);assert(capture_calls==1U);
        assert(ABoxCapture_Stop(2,2)==ABOX_HAL_OK && ABoxCapture_Release(2,2)==ABOX_HAL_OK);
        assert(ABoxTimer_Stop(2)==ABOX_HAL_OK);
        assert(ABoxEncoder_Configure(2,&en)==ABOX_HAL_OK && encoder_mode==2U && capture_filter==7U);
        assert(ABoxPwm_Configure(2,3,1U,0U)==ABOX_HAL_BUSY);
        assert(ABoxTimer_Start(2,0)==ABOX_HAL_OK && (active_channels(2)&3U)==3U);
        assert(ABoxEncoder_Release(2)==ABOX_HAL_BUSY && ABoxTimer_SetCounter(2,123U)==ABOX_HAL_OK);
        assert(ABoxTimer_Read(2,&value)==ABOX_HAL_OK && value==123U);
        assert(ABoxTimer_Stop(2)==ABOX_HAL_OK && !(active_channels(2)&3U));
        assert(ABoxEncoder_Release(2)==ABOX_HAL_OK && ABoxPwm_Configure(2,1,20U,0U)==ABOX_HAL_OK);
        assert(ABoxPwm_Release(2,1)==ABOX_HAL_OK && ABoxTimer_Close(2)==ABOX_HAL_OK);
    }
    return 0;
}
