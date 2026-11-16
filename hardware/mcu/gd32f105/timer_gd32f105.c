#include <string.h>
#include "abox_timer_common.h"
#include "abox_capture_dma_backend.h"
#include "abox_pwm_dma_backend.h"
#include "abox_timer_burst_backend.h"
#include "gd32f10x.h"

typedef struct {
    ABoxTimerConfig config;
    const ABoxTimerEvents *events;
    const ABoxTimerBreakEvents *break_events;
    uint32_t burst_dcr;
    const ABoxCaptureEvents *capture_events[4];
    const ABoxCompareEvents *compare_events[4];
    uint8_t open, running, configured, enabled, capture_configured, capture_active, capture_dma, encoder;
    uint8_t pwm_dma, complementary, advanced, n_low, idle, n_idle, slave_mode, hall, compare;
} TimerInstance;
static TimerInstance g_timer[7];
static uint32_t timer(unsigned id)
{
    switch (id) { case 1: return TIMER0; case 2: return TIMER1; case 3: return TIMER2; case 4: return TIMER3;
                  case 5: return TIMER4; case 6: return TIMER5; case 7: return TIMER6; default: return 0U; }
}
static rcu_periph_enum clock_id(unsigned id)
{
    static const rcu_periph_enum ids[] = {RCU_TIMER0,RCU_TIMER1,RCU_TIMER2,RCU_TIMER3,RCU_TIMER4,RCU_TIMER5,RCU_TIMER6};
    return ids[id - 1U];
}
static uint16_t alignment(ABoxTimerMode mode)
{
    switch (mode) { case ABOX_TIMER_CENTER_DOWN: return TIMER_COUNTER_CENTER_DOWN;
    case ABOX_TIMER_CENTER_UP: return TIMER_COUNTER_CENTER_UP;
    case ABOX_TIMER_CENTER_BOTH: return TIMER_COUNTER_CENTER_BOTH;
    default: return TIMER_COUNTER_EDGE; }
}

ABoxHalStatus ABoxTimer_Open(unsigned id, const ABoxTimerConfig *config)
{
    TimerInstance *s;
    timer_parameter_struct p;
    if (!ABoxTimer_ConfigValid(id, config)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (s->open) return ABOX_HAL_BUSY;
    rcu_periph_clock_enable(clock_id(id));
    timer_deinit(timer(id));
    timer_struct_para_init(&p);
    p.prescaler = (uint16_t)(config->prescaler - 1U);
    p.period = config->period - 1U;
    p.counterdirection = config->mode == ABOX_TIMER_DOWN ? TIMER_COUNTER_DOWN : TIMER_COUNTER_UP;
    p.alignedmode = alignment(config->mode);
    p.clockdivision = TIMER_CKDIV_DIV1;
    p.repetitioncounter = 0U;
    timer_init(timer(id), &p);
    if (id<=5U) TIMER_CTL1(timer(id)) &= ~TIMER_CTL1_TI0S;
    memset(s, 0, sizeof(*s)); s->config = *config; s->open = 1U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Start(unsigned id, const ABoxTimerEvents *events)
{
    TimerInstance *s;
    if (!timer(id)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running) return ABOX_HAL_BUSY;
    timer_flag_clear(timer(id), TIMER_FLAG_UP);
    s->events = events; s->running = 1U;
    if (s->encoder) {
        timer_channel_output_state_config(timer(id), 0U, TIMER_CCX_ENABLE);
        timer_channel_output_state_config(timer(id), 1U, TIMER_CCX_ENABLE);
    }
    if (events && events->update) timer_interrupt_enable(timer(id), TIMER_INT_UP);
    if (s->slave_mode==ABOX_TIMER_SLAVE_TRIGGER)
        TIMER_SMCFG(timer(id))=(TIMER_SMCFG(timer(id)) & ~TIMER_SMCFG_SMC)|ABOX_TIMER_SLAVE_TRIGGER;
    else timer_enable(timer(id));
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Stop(unsigned id)
{
    TimerInstance *s;
    if (!timer(id)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->enabled || s->complementary || s->capture_active || s->pwm_dma) return ABOX_HAL_BUSY;
    if (s->running) {
        if (s->slave_mode==ABOX_TIMER_SLAVE_TRIGGER) TIMER_SMCFG(timer(id)) &= ~TIMER_SMCFG_SMC;
        if (s->encoder) {
            timer_channel_output_state_config(timer(id), 0U, TIMER_CCX_DISABLE);
            timer_channel_output_state_config(timer(id), 1U, TIMER_CCX_DISABLE);
        }
        timer_interrupt_disable(timer(id), TIMER_INT_UP);
        timer_disable(timer(id));
    }
    s->running = 0U; s->events = 0;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Close(unsigned id)
{
    TimerInstance *s;
    if (!timer(id)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->enabled || s->complementary || s->capture_active || s->pwm_dma) return ABOX_HAL_BUSY;
    if (id==1U) timer_interrupt_disable(timer(id),TIMER_INT_BRK);
    timer_deinit(timer(id)); rcu_periph_clock_disable(clock_id(id)); s->open = 0U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Reset(unsigned id)
{
    static const rcu_periph_reset_enum reset[]={RCU_TIMER0RST,RCU_TIMER1RST,RCU_TIMER2RST,
        RCU_TIMER3RST,RCU_TIMER4RST,RCU_TIMER5RST,RCU_TIMER6RST};
    if (!timer(id)) return ABOX_HAL_INVALID;
    if (g_timer[id-1U].open) return ABOX_HAL_BUSY;
    rcu_periph_reset_enable(reset[id-1U]);rcu_periph_reset_disable(reset[id-1U]);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Read(unsigned id, uint32_t *counter)
{
    if (!timer(id) || !counter || !g_timer[id - 1U].open) return ABOX_HAL_INVALID;
    *counter = timer_counter_read(timer(id));
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetCounter(unsigned id, uint32_t counter)
{
    TimerInstance *s;
    if (!timer(id)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (!s->open || counter >= s->config.period) return ABOX_HAL_INVALID;
    timer_counter_value_config(timer(id), counter);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetTrigger(unsigned id,ABoxTimerTrigger trigger)
{
    TimerInstance *s;
    if (!timer(id) || (unsigned)trigger>ABOX_TIMER_TRGO_OC4 ||
        (id>=6U && trigger>ABOX_TIMER_TRGO_UPDATE)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running) return ABOX_HAL_BUSY;
    timer_master_output_trigger_source_select(timer(id),(uint32_t)trigger<<4U);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetCaptureDmaOnUpdate(unsigned id,uint8_t enabled)
{
    TimerInstance *s;
    if (!timer(id) || enabled>1U) return ABOX_HAL_INVALID;
    if (id>5U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->pwm_dma || s->capture_dma) return ABOX_HAL_BUSY;
    if (enabled) TIMER_CTL1(timer(id))|=1U<<3U;
    else TIMER_CTL1(timer(id))&=~(1U<<3U);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetOnePulse(unsigned id,uint8_t enabled)
{
    TimerInstance *s;
    if (!timer(id) || enabled>1U) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->encoder) return ABOX_HAL_BUSY;
    if (enabled) TIMER_CTL0(timer(id)) |= TIMER_CTL0_SPM;
    else TIMER_CTL0(timer(id)) &= ~TIMER_CTL0_SPM;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetRepetition(unsigned id,uint8_t repetition)
{
    TimerInstance *s;
    if(!timer(id))return ABOX_HAL_INVALID;
    if(id!=1U)return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if(!s->open)return ABOX_HAL_INVALID;
    if(s->running || s->pwm_dma || s->capture_active)return ABOX_HAL_BUSY;
    TIMER_CREP(timer(id))=repetition;TIMER_SWEVG(timer(id))=TIMER_SWEVG_UPG;
    timer_flag_clear(timer(id),TIMER_FLAG_UP);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetSlave(unsigned id,const ABoxTimerSlaveConfig *config)
{
    TimerInstance *s;uint32_t bits;
    if (!ABoxTimer_SlaveConfigValid(id,config)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->encoder) return ABOX_HAL_BUSY;
    bits=ABoxTimer_SlaveBits(config);
    if (config->mode==ABOX_TIMER_SLAVE_TRIGGER) bits &= ~TIMER_SMCFG_SMC;
    TIMER_SMCFG(timer(id))=(TIMER_SMCFG(timer(id)) & ~0xff77U)|bits;
    s->slave_mode=(uint8_t)config->mode;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetHallInput(unsigned id,uint8_t enabled)
{
    TimerInstance *s;
    if (!timer(id) || enabled>1U) return ABOX_HAL_INVALID;
    if (id>5U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->capture_active || s->encoder) return ABOX_HAL_BUSY;
    if (enabled) TIMER_CTL1(timer(id)) |= TIMER_CTL1_TI0S;
    else TIMER_CTL1(timer(id)) &= ~TIMER_CTL1_TI0S;
    s->hall=enabled;return ABOX_HAL_OK;
}
static void advanced_output(TimerInstance *s,unsigned channel)
{
    uint32_t bit=1U<<(channel-1U),shift=2U*(channel-1U),idle_mask=1U<<(8U+shift),t=timer(1U);
    TIMER_CTL1(t)=(TIMER_CTL1(t) & ~idle_mask) | ((s->idle & bit) ? idle_mask : 0U);
    if (channel<=3U) {
        uint32_t n_idle_mask=idle_mask<<1U,n_pol_mask=TIMER_CHCTL2_CH0NP<<(4U*(channel-1U));
        TIMER_CTL1(t)=(TIMER_CTL1(t) & ~n_idle_mask) | ((s->n_idle & bit) ? n_idle_mask : 0U);
        TIMER_CHCTL2(t)=(TIMER_CHCTL2(t) & ~n_pol_mask) | ((s->n_low & bit) ? n_pol_mask : 0U);
    }
}
ABoxHalStatus ABoxTimer_ConfigureBreak(unsigned id,const ABoxTimerBreakConfig *config)
{
    TimerInstance *s;
    if (!timer(id) || !ABoxTimer_BreakConfigValid(config)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->enabled || s->complementary) return ABOX_HAL_BUSY;
    TIMER_CCHP(timer(id))=ABoxTimer_BreakBits(config);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_BreakPending(unsigned id,uint8_t *pending)
{
    if (!timer(id) || !pending) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    if (!g_timer[0].open) return ABOX_HAL_INVALID;
    *pending=timer_flag_get(timer(id),TIMER_FLAG_BRK)!=RESET ? 1U : 0U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_ClearBreak(unsigned id)
{
    if (!timer(id)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    if (!g_timer[0].open) return ABOX_HAL_INVALID;
    timer_flag_clear(timer(id),TIMER_FLAG_BRK);
    if (g_timer[0].break_events) timer_interrupt_enable(timer(id),TIMER_INT_BRK);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetBreakEvents(unsigned id,const ABoxTimerBreakEvents *events)
{
    TimerInstance *s;
    if (!timer(id) || (events && !events->detected)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if (!s->open) return ABOX_HAL_INVALID;
    timer_interrupt_disable(timer(id),TIMER_INT_BRK);s->break_events=events;
    if (events) timer_interrupt_enable(timer(id),TIMER_INT_BRK);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_OutputsArmed(unsigned id,uint8_t *armed)
{
    if (!timer(id) || !armed) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    if (!g_timer[0].open) return ABOX_HAL_INVALID;
    *armed=(TIMER_CCHP(timer(id)) & TIMER_CCHP_POEN) ? 1U : 0U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_RearmOutputs(unsigned id)
{
    TimerInstance *s;
    if (!timer(id)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if (!s->open || (!s->enabled && !s->complementary)) return ABOX_HAL_INVALID;
    if (timer_flag_get(timer(id),TIMER_FLAG_BRK)!=RESET) return ABOX_HAL_BUSY;
    timer_primary_output_config(timer(id),ENABLE);
    return (TIMER_CCHP(timer(id)) & TIMER_CCHP_POEN) ? ABOX_HAL_OK : ABOX_HAL_BUSY;
}
static ABoxHalStatus capture_read(unsigned id, unsigned channel, uint32_t *counter, uint8_t *overcapture)
{
    uint32_t flag=TIMER_FLAG_CH0 << (channel-1U), over=TIMER_FLAG_CH0O << (channel-1U);
    uint8_t lost;
    if (timer_flag_get(timer(id),flag)==RESET) return ABOX_HAL_BUSY;
    lost=timer_flag_get(timer(id),over)!=RESET ? 1U : 0U;
    *counter=timer_channel_capture_value_register_read(timer(id),(uint16_t)(channel-1U));
    if (lost) timer_flag_clear(timer(id),over);
    *overcapture=lost; return ABOX_HAL_OK;
}
void ABoxTimer_Irq(unsigned id)
{
    TimerInstance *s; const ABoxTimerEvents *e;
    if (!timer(id)) return;
    s=&g_timer[id-1U]; e=s->events;
    if (!s->open) return;
    if (id==1U && s->break_events && timer_interrupt_flag_get(timer(id),TIMER_INT_FLAG_BRK)!=RESET) {
        const ABoxTimerBreakEvents *event=s->break_events;
        timer_interrupt_disable(timer(id),TIMER_INT_BRK);event->detected(event->context,id);
    }
    if (!s->running) return;
    if (e && e->update && timer_interrupt_flag_get(timer(id),TIMER_INT_FLAG_UP)!=RESET) {
        timer_interrupt_flag_clear(timer(id),TIMER_INT_FLAG_UP);
        e->update(e->context,id);
    }
    for (unsigned channel=1U;channel<=4U;++channel) {
        const ABoxCaptureEvents *capture=s->capture_events[channel-1U];
        uint32_t value; uint8_t lost;
        if (capture && capture->captured && (s->capture_active & (1U<<(channel-1U))) &&
            timer_interrupt_flag_get(timer(id),TIMER_INT_FLAG_CH0 << (channel-1U))!=RESET &&
            capture_read(id,channel,&value,&lost)==ABOX_HAL_OK)
            capture->captured(capture->context,id,channel,value,lost);
        if (s->compare_events[channel-1U] && (s->enabled & s->compare & (1U<<(channel-1U))) &&
            timer_interrupt_flag_get(timer(id),TIMER_INT_FLAG_CH0<<(channel-1U))!=RESET) {
            const ABoxCompareEvents *event=s->compare_events[channel-1U];
            timer_interrupt_flag_clear(timer(id),TIMER_INT_FLAG_CH0<<(channel-1U));
            event->matched(event->context,id,channel);
        }
    }
}
ABoxHalStatus ABoxPwm_Configure(unsigned id, unsigned channel, uint32_t pulse, uint8_t active_low)
{
    TimerInstance *s;
    timer_oc_parameter_struct oc;
    uint8_t bit;
    uint16_t selected_mode;
    if (!ABoxTimer_PwmValid(id, channel) || active_low > 1U) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U]; bit = (uint8_t)(1U << (channel - 1U));
    if (!s->open || pulse > s->config.period) return ABOX_HAL_INVALID;
    if (((s->enabled|s->complementary|s->compare) & bit) || (s->pwm_dma & bit) || (s->capture_configured & bit) || s->encoder) return ABOX_HAL_BUSY;
    timer_channel_output_struct_para_init(&oc);
    oc.outputstate = TIMER_CCX_DISABLE;
    oc.ocpolarity = active_low ? TIMER_OC_POLARITY_LOW : TIMER_OC_POLARITY_HIGH;
    timer_channel_output_shadow_config(timer(id), (uint16_t)(channel - 1U), TIMER_OC_SHADOW_DISABLE);
    timer_channel_output_config(timer(id), (uint16_t)(channel - 1U), &oc);
    if (id==1U) advanced_output(s,channel);
    selected_mode = pulse == 0U ? TIMER_OC_MODE_LOW :
                    pulse == s->config.period ? TIMER_OC_MODE_HIGH : TIMER_OC_MODE_PWM0;
    timer_channel_output_mode_config(timer(id), (uint16_t)(channel - 1U), selected_mode);
    timer_channel_output_pulse_value_config(timer(id), (uint16_t)(channel - 1U),
                                            (uint16_t)(pulse == s->config.period ? 0U : pulse));
    timer_channel_output_shadow_config(timer(id), (uint16_t)(channel - 1U), TIMER_OC_SHADOW_DISABLE);
    s->configured |= bit;
    return ABOX_HAL_OK;
}
static ABoxHalStatus output_enable(unsigned id, unsigned channel)
{
    TimerInstance *s;
    uint8_t bit;
    if (!ABoxTimer_PwmValid(id, channel)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U]; bit = (uint8_t)(1U << (channel - 1U));
    if (!s->open || !(s->configured & bit)) return ABOX_HAL_INVALID;
    if ((s->enabled & bit) || (id==1U && timer_flag_get(timer(id),TIMER_FLAG_BRK)!=RESET)) return ABOX_HAL_BUSY;
    timer_channel_output_state_config(timer(id), (uint16_t)(channel - 1U), TIMER_CCX_ENABLE);
    if (id == 1U) {
        timer_primary_output_config(timer(id), ENABLE);
        if (!(TIMER_CCHP(timer(id)) & TIMER_CCHP_POEN)) {
            timer_channel_output_state_config(timer(id),(uint16_t)(channel-1U),TIMER_CCX_DISABLE);return ABOX_HAL_BUSY;
        }
    }
    s->enabled |= bit;
    return ABOX_HAL_OK;
}
static ABoxHalStatus output_disable(unsigned id, unsigned channel)
{
    TimerInstance *s;
    uint8_t bit;
    if (!ABoxTimer_PwmValid(id, channel)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U]; bit = (uint8_t)(1U << (channel - 1U));
    if (!s->open || !(s->enabled & bit)) return ABOX_HAL_INVALID;
    timer_channel_output_state_config(timer(id), (uint16_t)(channel - 1U), TIMER_CCX_DISABLE);
    s->enabled &= (uint8_t)~bit;
    if (id == 1U && !s->enabled && !s->complementary) timer_primary_output_config(timer(id), DISABLE);
    return ABOX_HAL_OK;
}

static ABoxHalStatus output_release(unsigned id, unsigned channel)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->configured & bit)) return ABOX_HAL_INVALID;
    if (((s->enabled|s->complementary) & bit) || (s->pwm_dma & bit)) return ABOX_HAL_BUSY;
    s->configured &= (uint8_t)~bit;s->advanced &= (uint8_t)~bit;
    s->idle &= (uint8_t)~bit;s->n_idle &= (uint8_t)~bit;s->n_low &= (uint8_t)~bit;return ABOX_HAL_OK;
}

ABoxHalStatus ABoxPwm_Enable(unsigned id,unsigned channel)
{ if (ABoxTimer_PwmValid(id,channel) && (g_timer[id-1U].compare & (1U<<(channel-1U)))) return ABOX_HAL_BUSY; return output_enable(id,channel); }
ABoxHalStatus ABoxPwm_Disable(unsigned id,unsigned channel)
{ if (ABoxTimer_PwmValid(id,channel) && (g_timer[id-1U].compare & (1U<<(channel-1U)))) return ABOX_HAL_BUSY; return output_disable(id,channel); }
ABoxHalStatus ABoxPwm_Release(unsigned id,unsigned channel)
{ if (ABoxTimer_PwmValid(id,channel) && (g_timer[id-1U].compare & (1U<<(channel-1U)))) return ABOX_HAL_BUSY; return output_release(id,channel); }

ABoxHalStatus ABoxCompare_Configure(unsigned id,unsigned channel,uint32_t compare,ABoxCompareMode mode,uint8_t low)
{
    static const uint16_t modes[]={TIMER_OC_MODE_TIMING,TIMER_OC_MODE_ACTIVE,TIMER_OC_MODE_INACTIVE,TIMER_OC_MODE_TOGGLE};
    TimerInstance *s;uint8_t bit;timer_oc_parameter_struct output;
    if (!ABoxTimer_PwmValid(id,channel) || (unsigned)mode>ABOX_COMPARE_TOGGLE || low>1U) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || compare>=s->config.period) return ABOX_HAL_INVALID;
    if (((s->configured & (uint8_t)~s->compare)|s->enabled|s->complementary|s->capture_configured|s->pwm_dma) & bit) return ABOX_HAL_BUSY;
    if (s->encoder) return ABOX_HAL_BUSY;
    timer_channel_output_struct_para_init(&output);output.outputstate=TIMER_CCX_DISABLE;
    output.ocpolarity=low ? TIMER_OC_POLARITY_LOW : TIMER_OC_POLARITY_HIGH;
    timer_channel_output_shadow_config(timer(id),(uint16_t)(channel-1U),TIMER_OC_SHADOW_DISABLE);
    timer_channel_output_config(timer(id),(uint16_t)(channel-1U),&output);
    timer_channel_output_mode_config(timer(id),(uint16_t)(channel-1U),modes[mode]);
    timer_channel_output_pulse_value_config(timer(id),(uint16_t)(channel-1U),(uint16_t)compare);
    s->configured|=bit;s->compare|=bit;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCompare_Start(unsigned id,unsigned channel,const ABoxCompareEvents *events)
{
    TimerInstance *s;uint8_t bit;ABoxHalStatus result;
    if (!ABoxTimer_PwmValid(id,channel) || (events && !events->matched)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->compare & bit)) return ABOX_HAL_INVALID;
    if (s->enabled & bit) return ABOX_HAL_BUSY;
    timer_flag_clear(timer(id),TIMER_FLAG_CH0<<(channel-1U));
    result=output_enable(id,channel);if (result!=ABOX_HAL_OK) return result;
    s->compare_events[channel-1U]=events;
    if (events) timer_interrupt_enable(timer(id),TIMER_INT_CH0<<(channel-1U));
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCompare_Poll(unsigned id,unsigned channel,uint8_t *matched)
{
    TimerInstance *s;uint8_t bit;uint32_t flag;
    if (!ABoxTimer_PwmValid(id,channel) || !matched) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->compare & s->enabled & bit)) return ABOX_HAL_INVALID;
    if (s->compare_events[channel-1U]) return ABOX_HAL_BUSY;
    flag=TIMER_FLAG_CH0<<(channel-1U);*matched=timer_flag_get(timer(id),flag)!=RESET;
    if (*matched) timer_flag_clear(timer(id),flag);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCompare_Stop(unsigned id,unsigned channel)
{
    TimerInstance *s;uint8_t bit;ABoxHalStatus result;
    if (!ABoxTimer_PwmValid(id,channel)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->compare & s->enabled & bit)) return ABOX_HAL_INVALID;
    timer_interrupt_disable(timer(id),TIMER_INT_CH0<<(channel-1U));
    result=output_disable(id,channel);s->compare_events[channel-1U]=0;return result;
}
ABoxHalStatus ABoxCompare_Release(unsigned id,unsigned channel)
{
    TimerInstance *s;uint8_t bit;ABoxHalStatus result;
    if (!ABoxTimer_PwmValid(id,channel)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->compare & bit)) return ABOX_HAL_INVALID;
    result=output_release(id,channel);if (result==ABOX_HAL_OK) s->compare &= (uint8_t)~bit;return result;
}

ABoxHalStatus ABoxPwm_ConfigureAdvancedOutput(unsigned id,unsigned channel,uint8_t idle,uint8_t n_idle,uint8_t n_low)
{
    TimerInstance *s;uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel) || idle>1U || n_idle>1U || n_low>1U ||
        (channel==4U && (n_idle || n_low))) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->configured & bit)) return ABOX_HAL_INVALID;
    if (((s->enabled|s->complementary|s->pwm_dma|s->compare) & bit)) return ABOX_HAL_BUSY;
    s->idle=(uint8_t)((s->idle & ~bit) | (idle ? bit : 0U));
    s->n_idle=(uint8_t)((s->n_idle & ~bit) | (n_idle ? bit : 0U));
    s->n_low=(uint8_t)((s->n_low & ~bit) | (n_low ? bit : 0U));
    s->advanced|=bit;advanced_output(s,channel);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxPwm_EnableComplementary(unsigned id,unsigned channel)
{
    TimerInstance *s;uint8_t bit;uint32_t enable;
    if (!ABoxTimer_PwmValid(id,channel) || channel>3U) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];bit=(uint8_t)(1U<<(channel-1U));enable=TIMER_CHCTL2_CH0NEN<<(4U*(channel-1U));
    if (!s->open || !(s->configured & bit) || !(s->advanced & bit)) return ABOX_HAL_INVALID;
    if ((s->complementary & bit) || timer_flag_get(timer(id),TIMER_FLAG_BRK)!=RESET) return ABOX_HAL_BUSY;
    TIMER_CHCTL2(timer(id)) |= enable;timer_primary_output_config(timer(id),ENABLE);
    if (!(TIMER_CCHP(timer(id)) & TIMER_CCHP_POEN)) { TIMER_CHCTL2(timer(id)) &= ~enable;return ABOX_HAL_BUSY; }
    s->complementary |= bit;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxPwm_DisableComplementary(unsigned id,unsigned channel)
{
    TimerInstance *s;uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel) || channel>3U) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->complementary & bit)) return ABOX_HAL_INVALID;
    TIMER_CHCTL2(timer(id)) &= ~(TIMER_CHCTL2_CH0NEN<<(4U*(channel-1U)));
    s->complementary &= (uint8_t)~bit;
    if (!s->enabled && !s->complementary) timer_primary_output_config(timer(id),DISABLE);
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxPwmHw_DmaReserve(unsigned id,unsigned channel,uint16_t initial,
                                   const uint16_t *samples,uint16_t count)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel) || !samples || !count) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->configured & bit) || (s->compare & bit) || initial>s->config.period) return ABOX_HAL_INVALID;
    if (s->running || ((s->enabled|s->complementary) & bit) || s->pwm_dma) return ABOX_HAL_BUSY;
    for (unsigned i=0U;i<count;++i) if (samples[i]>s->config.period) return ABOX_HAL_INVALID;
    s->pwm_dma=bit; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimerBurstHw_Reserve(unsigned id)
{
    TimerInstance *s;if (!timer(id)) return ABOX_HAL_INVALID;
    if (id>5U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->pwm_dma) return ABOX_HAL_BUSY;
    s->burst_dcr=TIMER_DMACFG(timer(id));s->pwm_dma=0x80U;return ABOX_HAL_OK;
}
void ABoxTimerBurstHw_Config(unsigned id,uint8_t base,uint8_t length)
{ TIMER_DMACFG(timer(id))=(uint32_t)base|((uint32_t)(length-1U)<<8U); }
void ABoxTimerBurstHw_Request(unsigned id,unsigned request,int enabled)
{
    uint32_t index=request==5U ? 6U : (request==6U ? 5U : request); /* TRIG and COM occupy bits 14 and 13. */
    uint32_t bit=1U<<(8U+index);
    if (enabled) timer_dma_enable(timer(id),bit); else timer_dma_disable(timer(id),bit);
}
uintptr_t ABoxTimerBurstHw_Address(unsigned id) { return (uintptr_t)&TIMER_DMATB(timer(id)); }
void ABoxTimerBurstHw_Release(unsigned id)
{ TimerInstance *s=&g_timer[id-1U];TIMER_DMACFG(timer(id))=s->burst_dcr;s->pwm_dma=0U; }
ABoxHalStatus ABoxPwmHw_DmaPrepare(unsigned id,unsigned channel,uint16_t initial)
{
    uint32_t t=timer(id); uint16_t ch=(uint16_t)(channel-1U);
    timer_channel_output_shadow_config(t,ch,TIMER_OC_SHADOW_DISABLE);
    timer_channel_output_mode_config(t,ch,TIMER_OC_MODE_PWM0);
    timer_channel_output_pulse_value_config(t,ch,initial);
    timer_channel_output_shadow_config(t,ch,TIMER_OC_SHADOW_ENABLE);
    return ABOX_HAL_OK;
}
uintptr_t ABoxPwmHw_DmaAddress(unsigned id,unsigned channel)
{ return (uintptr_t)&TIMER_CH0CV(timer(id))+(channel-1U)*sizeof(uint32_t); }
void ABoxPwmHw_DmaEnable(unsigned id) { timer_dma_enable(timer(id),TIMER_DMA_UPD); }
void ABoxPwmHw_DmaPause(unsigned id) { timer_dma_disable(timer(id),TIMER_DMA_UPD); }
void ABoxPwmHw_DmaRelease(unsigned id,unsigned channel)
{
    TimerInstance *s=&g_timer[id-1U];
    ABoxPwmHw_DmaPause(id); s->pwm_dma &= (uint8_t)~(1U<<(channel-1U));
}
ABoxHalStatus ABoxCapture_Configure(unsigned id, unsigned channel, const ABoxCaptureConfig *config)
{
    static const uint16_t psc[]={TIMER_IC_PSC_DIV1,TIMER_IC_PSC_DIV2,TIMER_IC_PSC_DIV4,TIMER_IC_PSC_DIV8};
    TimerInstance *s; timer_ic_parameter_struct ic; uint8_t bit; unsigned index=0U, divider;
    if (!ABoxCapture_ConfigValid(id,channel,config)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open) return ABOX_HAL_INVALID;
    if ((s->configured & bit) || (s->capture_active & bit) || s->encoder) return ABOX_HAL_BUSY;
    for (divider=config->prescaler; divider>1U; divider>>=1U) ++index;
    timer_channel_input_struct_para_init(&ic);
    ic.icpolarity=config->edge==ABOX_CAPTURE_RISING ? TIMER_IC_POLARITY_RISING : TIMER_IC_POLARITY_FALLING;
    ic.icselection=config->input==ABOX_CAPTURE_DIRECT ? TIMER_IC_SELECTION_DIRECTTI :
        config->input==ABOX_CAPTURE_INDIRECT ? TIMER_IC_SELECTION_INDIRECTTI : TIMER_IC_SELECTION_ITS;
    ic.icprescaler=psc[index]; ic.icfilter=config->filter;
    timer_input_capture_config(timer(id),(uint16_t)(channel-1U),&ic);
    /* The vendor initializer enables the channel; Open must leave it stopped. */
    timer_channel_output_state_config(timer(id),(uint16_t)(channel-1U),TIMER_CCX_DISABLE);
    s->capture_configured |= bit; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCapture_Start(unsigned id, unsigned channel, const ABoxCaptureEvents *events)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel) || (events && !events->captured)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->capture_configured & bit)) return ABOX_HAL_INVALID;
    if (s->capture_active & bit) return ABOX_HAL_BUSY;
    timer_flag_clear(timer(id),(TIMER_FLAG_CH0 | TIMER_FLAG_CH0O) << (channel-1U));
    s->capture_events[channel-1U]=events; s->capture_active |= bit;
    if (events) timer_interrupt_enable(timer(id),TIMER_INT_CH0 << (channel-1U));
    timer_channel_output_state_config(timer(id),(uint16_t)(channel-1U),TIMER_CCX_ENABLE);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCapture_Read(unsigned id, unsigned channel, uint32_t *counter, uint8_t *overcapture)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel) || !counter || !overcapture) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->capture_active & bit)) return ABOX_HAL_INVALID;
    if (s->capture_events[channel-1U] || (s->capture_dma & bit)) return ABOX_HAL_BUSY;
    return capture_read(id,channel,counter,overcapture);
}
ABoxHalStatus ABoxCapture_Stop(unsigned id, unsigned channel)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->capture_active & bit)) return ABOX_HAL_INVALID;
    if (s->capture_dma & bit) return ABOX_HAL_BUSY;
    timer_interrupt_disable(timer(id),TIMER_INT_CH0 << (channel-1U));
    timer_channel_output_state_config(timer(id),(uint16_t)(channel-1U),TIMER_CCX_DISABLE);
    s->capture_active &= (uint8_t)~bit; s->capture_events[channel-1U]=0; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCapture_Release(unsigned id, unsigned channel)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->capture_configured & bit)) return ABOX_HAL_INVALID;
    if (s->capture_active & bit) return ABOX_HAL_BUSY;
    s->capture_configured &= (uint8_t)~bit; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxEncoder_Configure(unsigned id, const ABoxEncoderConfig *config)
{
    TimerInstance *s; timer_ic_parameter_struct ic; uint32_t mode;
    if (!ABoxEncoder_ConfigValid(id,config)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->configured || s->capture_configured || s->encoder || s->slave_mode || s->hall ||
        (TIMER_CTL0(timer(id)) & TIMER_CTL0_SPM)) return ABOX_HAL_BUSY;
    if (s->config.mode != ABOX_TIMER_UP) return ABOX_HAL_UNSUPPORTED;
    timer_channel_input_struct_para_init(&ic);
    ic.icselection=TIMER_IC_SELECTION_DIRECTTI; ic.icprescaler=TIMER_IC_PSC_DIV1;
    ic.icpolarity=config->edge_a==ABOX_CAPTURE_RISING ? TIMER_IC_POLARITY_RISING : TIMER_IC_POLARITY_FALLING;
    ic.icfilter=config->filter_a; timer_input_capture_config(timer(id),0U,&ic);
    timer_channel_output_state_config(timer(id),0U,TIMER_CCX_DISABLE);
    ic.icpolarity=config->edge_b==ABOX_CAPTURE_RISING ? TIMER_IC_POLARITY_RISING : TIMER_IC_POLARITY_FALLING;
    ic.icfilter=config->filter_b; timer_input_capture_config(timer(id),1U,&ic);
    timer_channel_output_state_config(timer(id),1U,TIMER_CCX_DISABLE);
    mode=config->mode==ABOX_ENCODER_MODE1 ? TIMER_QUAD_DECODER_MODE0 :
         config->mode==ABOX_ENCODER_MODE2 ? TIMER_QUAD_DECODER_MODE1 : TIMER_QUAD_DECODER_MODE2;
    timer_quadrature_decoder_mode_config(timer(id),mode,
        config->edge_a==ABOX_CAPTURE_RISING ? TIMER_IC_POLARITY_RISING : TIMER_IC_POLARITY_FALLING,
        config->edge_b==ABOX_CAPTURE_RISING ? TIMER_IC_POLARITY_RISING : TIMER_IC_POLARITY_FALLING);
    s->encoder=1U; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxEncoder_Release(unsigned id)
{
    TimerInstance *s;
    if (id<1U || id>5U) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; if (!s->open || !s->encoder) return ABOX_HAL_INVALID;
    if (s->running) return ABOX_HAL_BUSY;
    timer_internal_clock_config(timer(id)); s->encoder=0U; return ABOX_HAL_OK;
}

ABoxHalStatus ABoxCaptureHw_DmaReserve(unsigned id,unsigned channel)
{
    TimerInstance *s;uint8_t bit;
    if(!ABoxTimer_PwmValid(id,channel))return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if(!s->open || !(s->capture_configured&bit))return ABOX_HAL_INVALID;
    if(s->capture_active&bit)return ABOX_HAL_BUSY;
    s->capture_active|=bit;s->capture_dma|=bit;return ABOX_HAL_OK;
}
uintptr_t ABoxCaptureHw_DmaAddress(unsigned id,unsigned channel)
{ return (uintptr_t)&TIMER_CH0CV(timer(id))+(channel-1U)*4U; }
void ABoxCaptureHw_DmaEnable(unsigned id,unsigned channel)
{
    uint32_t t=timer(id);
    timer_flag_clear(t,(TIMER_FLAG_CH0|TIMER_FLAG_CH0O)<<(channel-1U));
    timer_dma_enable(t,(uint16_t)(TIMER_DMA_CH0D<<(channel-1U)));
    timer_channel_output_state_config(t,(uint16_t)(channel-1U),TIMER_CCX_ENABLE);
}
void ABoxCaptureHw_DmaPause(unsigned id,unsigned channel)
{
    uint32_t t=timer(id);timer_dma_disable(t,(uint16_t)(TIMER_DMA_CH0D<<(channel-1U)));
    timer_channel_output_state_config(t,(uint16_t)(channel-1U),TIMER_CCX_DISABLE);
}
int ABoxCaptureHw_DmaOvercapture(unsigned id,unsigned channel)
{ return timer_flag_get(timer(id),TIMER_FLAG_CH0O<<(channel-1U))!=RESET; }
void ABoxCaptureHw_DmaRelease(unsigned id,unsigned channel)
{
    TimerInstance *s=&g_timer[id-1U];uint8_t bit=(uint8_t)(1U<<(channel-1U));
    ABoxCaptureHw_DmaPause(id,channel);
    timer_flag_clear(timer(id),(TIMER_FLAG_CH0|TIMER_FLAG_CH0O)<<(channel-1U));
    s->capture_active&=(uint8_t)~bit;s->capture_dma&=(uint8_t)~bit;
}
