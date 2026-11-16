#include <string.h>
#include "abox_timer_common.h"
#include "abox_capture_dma_backend.h"
#include "abox_pwm_dma_backend.h"
#include "abox_timer_burst_backend.h"
#include "stm32f1xx_hal.h"

typedef struct {
    TIM_HandleTypeDef handle;
    ABoxTimerConfig config;
    const ABoxTimerEvents *events;
    const ABoxTimerBreakEvents *break_events;
    uint32_t burst_dcr;
    const ABoxCaptureEvents *capture_events[4];
    const ABoxCompareEvents *compare_events[4];
    uint8_t open, running, configured, enabled, capture_configured, capture_active, capture_dma, encoder;
    uint8_t pwm_dma, pwm_low, complementary, advanced, n_low, idle, n_idle, slave_mode, hall, compare;
} TimerInstance;
static TimerInstance g_timer[7];
static TIM_TypeDef *timer(unsigned id)
{
    switch (id) { case 1: return TIM1; case 2: return TIM2; case 3: return TIM3; case 4: return TIM4;
                  case 5: return TIM5; case 6: return TIM6; case 7: return TIM7; default: return 0; }
}
static void clock_enable(unsigned id)
{
    switch (id) { case 1: __HAL_RCC_TIM1_CLK_ENABLE(); break; case 2: __HAL_RCC_TIM2_CLK_ENABLE(); break;
    case 3: __HAL_RCC_TIM3_CLK_ENABLE(); break; case 4: __HAL_RCC_TIM4_CLK_ENABLE(); break;
    case 5: __HAL_RCC_TIM5_CLK_ENABLE(); break; case 6: __HAL_RCC_TIM6_CLK_ENABLE(); break;
    case 7: __HAL_RCC_TIM7_CLK_ENABLE(); break; default: break; }
}
static void clock_disable(unsigned id)
{
    switch (id) { case 1: __HAL_RCC_TIM1_CLK_DISABLE(); break; case 2: __HAL_RCC_TIM2_CLK_DISABLE(); break;
    case 3: __HAL_RCC_TIM3_CLK_DISABLE(); break; case 4: __HAL_RCC_TIM4_CLK_DISABLE(); break;
    case 5: __HAL_RCC_TIM5_CLK_DISABLE(); break; case 6: __HAL_RCC_TIM6_CLK_DISABLE(); break;
    case 7: __HAL_RCC_TIM7_CLK_DISABLE(); break; default: break; }
}
static uint32_t mode(ABoxTimerMode m)
{
    switch (m) { case ABOX_TIMER_DOWN: return TIM_COUNTERMODE_DOWN;
    case ABOX_TIMER_CENTER_DOWN: return TIM_COUNTERMODE_CENTERALIGNED1;
    case ABOX_TIMER_CENTER_UP: return TIM_COUNTERMODE_CENTERALIGNED2;
    case ABOX_TIMER_CENTER_BOTH: return TIM_COUNTERMODE_CENTERALIGNED3;
    default: return TIM_COUNTERMODE_UP; }
}
static uint32_t channel_value(unsigned channel) { return (channel - 1U) * 4U; }

ABoxHalStatus ABoxTimer_Open(unsigned id, const ABoxTimerConfig *config)
{
    TimerInstance *s;
    if (!ABoxTimer_ConfigValid(id, config)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (s->open) return ABOX_HAL_BUSY;
    memset(s, 0, sizeof(*s));
    s->handle.Instance = timer(id);
    s->handle.Init.Prescaler = config->prescaler - 1U;
    s->handle.Init.Period = config->period - 1U;
    s->handle.Init.CounterMode = mode(config->mode);
    s->handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s->handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    s->handle.Init.RepetitionCounter = 0U;
    clock_enable(id);
    if (HAL_TIM_Base_Init(&s->handle) != HAL_OK) { clock_disable(id); return ABOX_HAL_ERROR; }
    s->handle.Instance->CR1 &= ~TIM_CR1_OPM;
    if (id<=5U) s->handle.Instance->CR2 &= ~TIM_CR2_TI1S;
    if (id==1U) { s->handle.Instance->BDTR=0U; __HAL_TIM_CLEAR_FLAG(&s->handle,TIM_FLAG_BREAK); }
    s->config = *config; s->open = 1U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Start(unsigned id, const ABoxTimerEvents *events)
{
    TimerInstance *s;
    HAL_StatusTypeDef result;
    if (!timer(id)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running) return ABOX_HAL_BUSY;
    s->events = events; s->running = 1U;
    if (s->encoder) {
        TIM_CCxChannelCmd(s->handle.Instance, channel_value(1U), TIM_CCx_ENABLE);
        TIM_CCxChannelCmd(s->handle.Instance, channel_value(2U), TIM_CCx_ENABLE);
    }
    __HAL_TIM_CLEAR_FLAG(&s->handle, TIM_FLAG_UPDATE);
    if (s->slave_mode==ABOX_TIMER_SLAVE_TRIGGER)
        s->handle.Instance->SMCR=(s->handle.Instance->SMCR & ~TIM_SMCR_SMS)|ABOX_TIMER_SLAVE_TRIGGER;
    result = events && events->update ? HAL_TIM_Base_Start_IT(&s->handle) : HAL_TIM_Base_Start(&s->handle);
    if (result == HAL_OK) return ABOX_HAL_OK;
    if (s->slave_mode==ABOX_TIMER_SLAVE_TRIGGER) s->handle.Instance->SMCR &= ~TIM_SMCR_SMS;
    if (s->encoder) {
        TIM_CCxChannelCmd(s->handle.Instance, channel_value(1U), TIM_CCx_DISABLE);
        TIM_CCxChannelCmd(s->handle.Instance, channel_value(2U), TIM_CCx_DISABLE);
    }
    s->running = 0U; s->events = 0;
    return result == HAL_BUSY ? ABOX_HAL_BUSY : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxTimer_Stop(unsigned id)
{
    TimerInstance *s;
    HAL_StatusTypeDef result;
    if (!timer(id)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->enabled || s->complementary || s->capture_active || s->pwm_dma) return ABOX_HAL_BUSY;
    if (!s->running) return ABOX_HAL_OK;
    if (s->slave_mode==ABOX_TIMER_SLAVE_TRIGGER) s->handle.Instance->SMCR &= ~TIM_SMCR_SMS;
    if (s->encoder) {
        TIM_CCxChannelCmd(s->handle.Instance, channel_value(1U), TIM_CCx_DISABLE);
        TIM_CCxChannelCmd(s->handle.Instance, channel_value(2U), TIM_CCx_DISABLE);
    }
    result = s->events && s->events->update ? HAL_TIM_Base_Stop_IT(&s->handle) : HAL_TIM_Base_Stop(&s->handle);
    if (result != HAL_OK) return ABOX_HAL_ERROR;
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
    if (id==1U) __HAL_TIM_DISABLE_IT(&s->handle,TIM_IT_BREAK);
    if (HAL_TIM_Base_DeInit(&s->handle) != HAL_OK) return ABOX_HAL_ERROR;
    clock_disable(id); s->open = 0U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Reset(unsigned id)
{
    if (!timer(id)) return ABOX_HAL_INVALID;
    if (g_timer[id-1U].open) return ABOX_HAL_BUSY;
    switch (id) {
    case 1U: __HAL_RCC_TIM1_FORCE_RESET();__HAL_RCC_TIM1_RELEASE_RESET();break;
    case 2U: __HAL_RCC_TIM2_FORCE_RESET();__HAL_RCC_TIM2_RELEASE_RESET();break;
    case 3U: __HAL_RCC_TIM3_FORCE_RESET();__HAL_RCC_TIM3_RELEASE_RESET();break;
    case 4U: __HAL_RCC_TIM4_FORCE_RESET();__HAL_RCC_TIM4_RELEASE_RESET();break;
    case 5U: __HAL_RCC_TIM5_FORCE_RESET();__HAL_RCC_TIM5_RELEASE_RESET();break;
    case 6U: __HAL_RCC_TIM6_FORCE_RESET();__HAL_RCC_TIM6_RELEASE_RESET();break;
    default: __HAL_RCC_TIM7_FORCE_RESET();__HAL_RCC_TIM7_RELEASE_RESET();break;
    }
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_Read(unsigned id, uint32_t *counter)
{
    if (!timer(id) || !counter || !g_timer[id - 1U].open) return ABOX_HAL_INVALID;
    *counter = __HAL_TIM_GET_COUNTER(&g_timer[id - 1U].handle);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetCounter(unsigned id, uint32_t counter)
{
    TimerInstance *s;
    if (!timer(id)) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U];
    if (!s->open || counter >= s->config.period) return ABOX_HAL_INVALID;
    __HAL_TIM_SET_COUNTER(&s->handle, counter);
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
    s->handle.Instance->CR2=(s->handle.Instance->CR2 & ~TIM_CR2_MMS)|((uint32_t)trigger<<4U);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetCaptureDmaOnUpdate(unsigned id,uint8_t enabled)
{
    TimerInstance *s;
    if (!timer(id) || enabled>1U) return ABOX_HAL_INVALID;
    if (id>5U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->pwm_dma || s->capture_dma) return ABOX_HAL_BUSY;
    if (enabled) s->handle.Instance->CR2|=1U<<3U;
    else s->handle.Instance->CR2&=~(1U<<3U);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetOnePulse(unsigned id,uint8_t enabled)
{
    TimerInstance *s;
    if (!timer(id) || enabled>1U) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->encoder) return ABOX_HAL_BUSY;
    if (enabled) s->handle.Instance->CR1 |= TIM_CR1_OPM;
    else s->handle.Instance->CR1 &= ~TIM_CR1_OPM;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetRepetition(unsigned id,uint8_t repetition)
{
    TimerInstance *s;
    if(!timer(id))return ABOX_HAL_INVALID;
    if(id!=1U)return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if(!s->open)return ABOX_HAL_INVALID;
    if(s->running || s->pwm_dma || s->capture_active)return ABOX_HAL_BUSY;
    s->handle.Init.RepetitionCounter=repetition;s->handle.Instance->RCR=repetition;
    s->handle.Instance->EGR=TIM_EGR_UG;__HAL_TIM_CLEAR_FLAG(&s->handle,TIM_FLAG_UPDATE);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetSlave(unsigned id,const ABoxTimerSlaveConfig *config)
{
    TimerInstance *s;uint32_t bits;
    if (!ABoxTimer_SlaveConfigValid(id,config)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->encoder) return ABOX_HAL_BUSY;
    bits=ABoxTimer_SlaveBits(config);
    if (config->mode==ABOX_TIMER_SLAVE_TRIGGER) bits &= ~TIM_SMCR_SMS;
    s->handle.Instance->SMCR=(s->handle.Instance->SMCR & ~0xff77U)|bits;
    s->slave_mode=(uint8_t)config->mode;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetHallInput(unsigned id,uint8_t enabled)
{
    TimerInstance *s;
    if (!timer(id) || enabled>1U) return ABOX_HAL_INVALID;
    if (id>5U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->capture_active || s->encoder) return ABOX_HAL_BUSY;
    if (enabled) s->handle.Instance->CR2 |= TIM_CR2_TI1S;
    else s->handle.Instance->CR2 &= ~TIM_CR2_TI1S;
    s->hall=enabled;return ABOX_HAL_OK;
}
static void advanced_output(TimerInstance *s,unsigned channel)
{
    uint32_t bit=1U<<(channel-1U),shift=2U*(channel-1U),idle_mask=1U<<(8U+shift);
    TIM_TypeDef *t=s->handle.Instance;
    t->CR2=(t->CR2 & ~idle_mask) | ((s->idle & bit) ? idle_mask : 0U);
    if (channel<=3U) {
        uint32_t n_idle_mask=idle_mask<<1U,n_pol_mask=TIM_CCER_CC1NP<<(4U*(channel-1U));
        t->CR2=(t->CR2 & ~n_idle_mask) | ((s->n_idle & bit) ? n_idle_mask : 0U);
        t->CCER=(t->CCER & ~n_pol_mask) | ((s->n_low & bit) ? n_pol_mask : 0U);
    }
}
ABoxHalStatus ABoxTimer_ConfigureBreak(unsigned id,const ABoxTimerBreakConfig *config)
{
    TimerInstance *s;
    if (!timer(id) || !ABoxTimer_BreakConfigValid(config)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->enabled || s->complementary) return ABOX_HAL_BUSY;
    s->handle.Instance->BDTR=ABoxTimer_BreakBits(config);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_BreakPending(unsigned id,uint8_t *pending)
{
    if (!timer(id) || !pending) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    if (!g_timer[0].open) return ABOX_HAL_INVALID;
    *pending=__HAL_TIM_GET_FLAG(&g_timer[0].handle,TIM_FLAG_BREAK) ? 1U : 0U; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_ClearBreak(unsigned id)
{
    if (!timer(id)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    if (!g_timer[0].open) return ABOX_HAL_INVALID;
    __HAL_TIM_CLEAR_FLAG(&g_timer[0].handle,TIM_FLAG_BREAK);
    if (g_timer[0].break_events) __HAL_TIM_ENABLE_IT(&g_timer[0].handle,TIM_IT_BREAK);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_SetBreakEvents(unsigned id,const ABoxTimerBreakEvents *events)
{
    TimerInstance *s;
    if (!timer(id) || (events && !events->detected)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if (!s->open) return ABOX_HAL_INVALID;
    __HAL_TIM_DISABLE_IT(&s->handle,TIM_IT_BREAK);s->break_events=events;
    if (events) __HAL_TIM_ENABLE_IT(&s->handle,TIM_IT_BREAK);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_OutputsArmed(unsigned id,uint8_t *armed)
{
    if (!timer(id) || !armed) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    if (!g_timer[0].open) return ABOX_HAL_INVALID;
    *armed=(g_timer[0].handle.Instance->BDTR & TIM_BDTR_MOE) ? 1U : 0U; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTimer_RearmOutputs(unsigned id)
{
    TimerInstance *s;
    if (!timer(id)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];if (!s->open || (!s->enabled && !s->complementary)) return ABOX_HAL_INVALID;
    if (__HAL_TIM_GET_FLAG(&s->handle,TIM_FLAG_BREAK)) return ABOX_HAL_BUSY;
    __HAL_TIM_MOE_ENABLE(&s->handle);
    return (s->handle.Instance->BDTR & TIM_BDTR_MOE) ? ABOX_HAL_OK : ABOX_HAL_BUSY;
}
static ABoxHalStatus capture_read(TimerInstance *s, unsigned channel, uint32_t *counter, uint8_t *overcapture)
{
    uint32_t flag = TIM_FLAG_CC1 << (channel - 1U);
    uint32_t over = TIM_FLAG_CC1OF << (channel - 1U);
    uint8_t lost;
    if (!__HAL_TIM_GET_FLAG(&s->handle, flag)) return ABOX_HAL_BUSY;
    lost = __HAL_TIM_GET_FLAG(&s->handle, over) ? 1U : 0U;
    *counter = HAL_TIM_ReadCapturedValue(&s->handle, channel_value(channel));
    if (lost) __HAL_TIM_CLEAR_FLAG(&s->handle, over);
    *overcapture = lost;
    return ABOX_HAL_OK;
}
void ABoxTimer_Irq(unsigned id)
{
    TimerInstance *s;
    const ABoxTimerEvents *e;
    if (!timer(id)) return;
    s = &g_timer[id - 1U]; e = s->events;
    if (!s->open) return;
    if (id==1U && s->break_events && __HAL_TIM_GET_IT_SOURCE(&s->handle,TIM_IT_BREAK) &&
        __HAL_TIM_GET_FLAG(&s->handle,TIM_FLAG_BREAK)) {
        const ABoxTimerBreakEvents *event=s->break_events;
        __HAL_TIM_DISABLE_IT(&s->handle,TIM_IT_BREAK);event->detected(event->context,id);
    }
    if (!s->running) return;
    if (e && e->update && __HAL_TIM_GET_IT_SOURCE(&s->handle, TIM_IT_UPDATE) &&
        __HAL_TIM_GET_FLAG(&s->handle, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_IT(&s->handle, TIM_IT_UPDATE);
        e->update(e->context, id);
    }
    for (unsigned channel = 1U; channel <= 4U; ++channel) {
        const ABoxCaptureEvents *capture = s->capture_events[channel - 1U];
        uint32_t value; uint8_t lost;
        if (capture && capture->captured && (s->capture_active & (1U << (channel - 1U))) &&
            __HAL_TIM_GET_IT_SOURCE(&s->handle, TIM_IT_CC1 << (channel - 1U)) &&
            capture_read(s, channel, &value, &lost) == ABOX_HAL_OK)
            capture->captured(capture->context, id, channel, value, lost);
        if (s->compare_events[channel-1U] && (s->enabled & s->compare & (1U<<(channel-1U))) &&
            __HAL_TIM_GET_IT_SOURCE(&s->handle,TIM_IT_CC1<<(channel-1U)) &&
            __HAL_TIM_GET_FLAG(&s->handle,TIM_FLAG_CC1<<(channel-1U))) {
            const ABoxCompareEvents *event=s->compare_events[channel-1U];
            __HAL_TIM_CLEAR_FLAG(&s->handle,TIM_FLAG_CC1<<(channel-1U));
            event->matched(event->context,id,channel);
        }
    }
}
ABoxHalStatus ABoxPwm_Configure(unsigned id, unsigned channel, uint32_t pulse, uint8_t active_low)
{
    TimerInstance *s;
    TIM_OC_InitTypeDef output = {0};
    uint8_t bit;
    if (!ABoxTimer_PwmValid(id, channel) || active_low > 1U) return ABOX_HAL_INVALID;
    s = &g_timer[id - 1U]; bit = (uint8_t)(1U << (channel - 1U));
    if (!s->open || pulse > s->config.period) return ABOX_HAL_INVALID;
    if (((s->enabled|s->complementary|s->compare) & bit) || (s->pwm_dma & bit) || (s->capture_configured & bit) || s->encoder) return ABOX_HAL_BUSY;
    output.OCMode = pulse == 0U ? TIM_OCMODE_FORCED_INACTIVE :
                    pulse == s->config.period ? TIM_OCMODE_FORCED_ACTIVE : TIM_OCMODE_PWM1;
    output.Pulse = pulse == s->config.period ? 0U : pulse;
    output.OCPolarity = active_low ? TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    output.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    output.OCFastMode = TIM_OCFAST_DISABLE;
    output.OCIdleState = TIM_OCIDLESTATE_RESET;
    output.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    __HAL_TIM_DISABLE_OCxPRELOAD(&s->handle, channel_value(channel));
    if (HAL_TIM_OC_ConfigChannel(&s->handle, &output, channel_value(channel)) != HAL_OK) return ABOX_HAL_ERROR;
    if (id==1U) advanced_output(s,channel);
    if (active_low) s->pwm_low |= bit; else s->pwm_low &= (uint8_t)~bit;
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
    if ((s->enabled & bit) || (id==1U && __HAL_TIM_GET_FLAG(&s->handle,TIM_FLAG_BREAK))) return ABOX_HAL_BUSY;
    TIM_CCxChannelCmd(s->handle.Instance, channel_value(channel), TIM_CCx_ENABLE);
    if (id == 1U) {
        __HAL_TIM_MOE_ENABLE(&s->handle);
        if (!(s->handle.Instance->BDTR & TIM_BDTR_MOE)) {
            TIM_CCxChannelCmd(s->handle.Instance,channel_value(channel),TIM_CCx_DISABLE);return ABOX_HAL_BUSY;
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
    TIM_CCxChannelCmd(s->handle.Instance, channel_value(channel), TIM_CCx_DISABLE);
    s->enabled &= (uint8_t)~bit;
    if (id == 1U && !s->enabled && !s->complementary) __HAL_TIM_MOE_DISABLE(&s->handle);
    return ABOX_HAL_OK;
}

static ABoxHalStatus output_release(unsigned id, unsigned channel)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id, channel)) return ABOX_HAL_INVALID;
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
    static const uint32_t modes[]={TIM_OCMODE_TIMING,TIM_OCMODE_ACTIVE,TIM_OCMODE_INACTIVE,TIM_OCMODE_TOGGLE};
    TimerInstance *s;uint8_t bit;TIM_OC_InitTypeDef output={0};
    if (!ABoxTimer_PwmValid(id,channel) || (unsigned)mode>ABOX_COMPARE_TOGGLE || low>1U) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || compare>=s->config.period) return ABOX_HAL_INVALID;
    if (((s->configured & (uint8_t)~s->compare)|s->enabled|s->complementary|s->capture_configured|s->pwm_dma) & bit) return ABOX_HAL_BUSY;
    if (s->encoder) return ABOX_HAL_BUSY;
    output.OCMode=modes[mode];output.Pulse=compare;
    output.OCPolarity=low ? TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    output.OCNPolarity=TIM_OCNPOLARITY_HIGH;output.OCFastMode=TIM_OCFAST_DISABLE;
    output.OCIdleState=TIM_OCIDLESTATE_RESET;output.OCNIdleState=TIM_OCNIDLESTATE_RESET;
    __HAL_TIM_DISABLE_OCxPRELOAD(&s->handle,channel_value(channel));
    if (HAL_TIM_OC_ConfigChannel(&s->handle,&output,channel_value(channel))!=HAL_OK) return ABOX_HAL_ERROR;
    s->configured|=bit;s->compare|=bit;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCompare_Start(unsigned id,unsigned channel,const ABoxCompareEvents *events)
{
    TimerInstance *s;uint8_t bit;ABoxHalStatus result;
    if (!ABoxTimer_PwmValid(id,channel) || (events && !events->matched)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->compare & bit)) return ABOX_HAL_INVALID;
    if (s->enabled & bit) return ABOX_HAL_BUSY;
    __HAL_TIM_CLEAR_FLAG(&s->handle,TIM_FLAG_CC1<<(channel-1U));
    result=output_enable(id,channel);if (result!=ABOX_HAL_OK) return result;
    s->compare_events[channel-1U]=events;
    if (events) __HAL_TIM_ENABLE_IT(&s->handle,TIM_IT_CC1<<(channel-1U));
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCompare_Poll(unsigned id,unsigned channel,uint8_t *matched)
{
    TimerInstance *s;uint8_t bit;uint32_t flag;
    if (!ABoxTimer_PwmValid(id,channel) || !matched) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->compare & s->enabled & bit)) return ABOX_HAL_INVALID;
    if (s->compare_events[channel-1U]) return ABOX_HAL_BUSY;
    flag=TIM_FLAG_CC1<<(channel-1U);*matched=__HAL_TIM_GET_FLAG(&s->handle,flag) ? 1U : 0U;
    if (*matched) __HAL_TIM_CLEAR_FLAG(&s->handle,flag);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCompare_Stop(unsigned id,unsigned channel)
{
    TimerInstance *s;uint8_t bit;ABoxHalStatus result;
    if (!ABoxTimer_PwmValid(id,channel)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->compare & s->enabled & bit)) return ABOX_HAL_INVALID;
    __HAL_TIM_DISABLE_IT(&s->handle,TIM_IT_CC1<<(channel-1U));
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
    s=&g_timer[0];bit=(uint8_t)(1U<<(channel-1U));enable=TIM_CCER_CC1NE<<(4U*(channel-1U));
    if (!s->open || !(s->configured & bit) || !(s->advanced & bit)) return ABOX_HAL_INVALID;
    if ((s->complementary & bit) || __HAL_TIM_GET_FLAG(&s->handle,TIM_FLAG_BREAK)) return ABOX_HAL_BUSY;
    s->handle.Instance->CCER |= enable;__HAL_TIM_MOE_ENABLE(&s->handle);
    if (!(s->handle.Instance->BDTR & TIM_BDTR_MOE)) { s->handle.Instance->CCER &= ~enable;return ABOX_HAL_BUSY; }
    s->complementary |= bit;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxPwm_DisableComplementary(unsigned id,unsigned channel)
{
    TimerInstance *s;uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel) || channel>3U) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_timer[0];bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->complementary & bit)) return ABOX_HAL_INVALID;
    s->handle.Instance->CCER &= ~(TIM_CCER_CC1NE<<(4U*(channel-1U)));
    s->complementary &= (uint8_t)~bit;
    if (!s->enabled && !s->complementary) __HAL_TIM_MOE_DISABLE(&s->handle);
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
    s->burst_dcr=s->handle.Instance->DCR;s->pwm_dma=0x80U;return ABOX_HAL_OK;
}
void ABoxTimerBurstHw_Config(unsigned id,uint8_t base,uint8_t length)
{ g_timer[id-1U].handle.Instance->DCR=(uint32_t)base|((uint32_t)(length-1U)<<8U); }
void ABoxTimerBurstHw_Request(unsigned id,unsigned request,int enabled)
{
    uint32_t index=request==5U ? 6U : (request==6U ? 5U : request); /* TRIG and COM occupy bits 14 and 13. */
    uint32_t bit=1U<<(8U+index);
    if (enabled) g_timer[id-1U].handle.Instance->DIER|=bit;
    else g_timer[id-1U].handle.Instance->DIER&=~bit;
}
uintptr_t ABoxTimerBurstHw_Address(unsigned id) { return (uintptr_t)&g_timer[id-1U].handle.Instance->DMAR; }
void ABoxTimerBurstHw_Release(unsigned id)
{ TimerInstance *s=&g_timer[id-1U];s->handle.Instance->DCR=s->burst_dcr;s->pwm_dma=0U; }
ABoxHalStatus ABoxPwmHw_DmaPrepare(unsigned id,unsigned channel,uint16_t initial)
{
    TimerInstance *s=&g_timer[id-1U]; uint8_t bit=(uint8_t)(1U<<(channel-1U));
    TIM_OC_InitTypeDef output={0};
    output.OCMode=TIM_OCMODE_PWM1; output.Pulse=initial;
    output.OCPolarity=(s->pwm_low & bit) ? TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    output.OCNPolarity=TIM_OCNPOLARITY_HIGH; output.OCFastMode=TIM_OCFAST_DISABLE;
    output.OCIdleState=TIM_OCIDLESTATE_RESET; output.OCNIdleState=TIM_OCNIDLESTATE_RESET;
    __HAL_TIM_DISABLE_OCxPRELOAD(&s->handle,channel_value(channel));
    if (HAL_TIM_OC_ConfigChannel(&s->handle,&output,channel_value(channel))!=HAL_OK) {
        s->configured &= (uint8_t)~bit; return ABOX_HAL_ERROR;
    }
    if (id==1U) advanced_output(s,channel);
    __HAL_TIM_ENABLE_OCxPRELOAD(&s->handle,channel_value(channel));
    return ABOX_HAL_OK;
}
uintptr_t ABoxPwmHw_DmaAddress(unsigned id,unsigned channel)
{ return (uintptr_t)&g_timer[id-1U].handle.Instance->CCR1 + (channel-1U)*sizeof(uint32_t); }
void ABoxPwmHw_DmaEnable(unsigned id) { __HAL_TIM_ENABLE_DMA(&g_timer[id-1U].handle,TIM_DMA_UPDATE); }
void ABoxPwmHw_DmaPause(unsigned id) { __HAL_TIM_DISABLE_DMA(&g_timer[id-1U].handle,TIM_DMA_UPDATE); }
void ABoxPwmHw_DmaRelease(unsigned id,unsigned channel)
{
    TimerInstance *s=&g_timer[id-1U];
    ABoxPwmHw_DmaPause(id); s->pwm_dma &= (uint8_t)~(1U<<(channel-1U));
}
ABoxHalStatus ABoxCapture_Configure(unsigned id, unsigned channel, const ABoxCaptureConfig *config)
{
    static const uint32_t psc[]={TIM_ICPSC_DIV1,TIM_ICPSC_DIV2,TIM_ICPSC_DIV4,TIM_ICPSC_DIV8};
    TimerInstance *s; TIM_IC_InitTypeDef ic={0}; uint8_t bit; unsigned index=0U, divider;
    if (!ABoxCapture_ConfigValid(id,channel,config)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open) return ABOX_HAL_INVALID;
    if ((s->configured & bit) || (s->capture_active & bit) || s->encoder) return ABOX_HAL_BUSY;
    for (divider=config->prescaler; divider>1U; divider>>=1U) ++index;
    ic.ICPolarity=config->edge==ABOX_CAPTURE_RISING ? TIM_ICPOLARITY_RISING : TIM_ICPOLARITY_FALLING;
    ic.ICSelection=config->input==ABOX_CAPTURE_DIRECT ? TIM_ICSELECTION_DIRECTTI :
        config->input==ABOX_CAPTURE_INDIRECT ? TIM_ICSELECTION_INDIRECTTI : TIM_ICSELECTION_TRC;
    ic.ICPrescaler=psc[index]; ic.ICFilter=config->filter;
    if (HAL_TIM_IC_ConfigChannel(&s->handle,&ic,channel_value(channel))!=HAL_OK) return ABOX_HAL_ERROR;
    s->capture_configured |= bit; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCapture_Start(unsigned id, unsigned channel, const ABoxCaptureEvents *events)
{
    TimerInstance *s; uint8_t bit; uint32_t flag;
    if (!ABoxTimer_PwmValid(id,channel) || (events && !events->captured)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->capture_configured & bit)) return ABOX_HAL_INVALID;
    if (s->capture_active & bit) return ABOX_HAL_BUSY;
    flag=(TIM_FLAG_CC1 | TIM_FLAG_CC1OF) << (channel-1U);
    __HAL_TIM_CLEAR_FLAG(&s->handle,flag);
    s->capture_events[channel-1U]=events; s->capture_active |= bit;
    if (events) __HAL_TIM_ENABLE_IT(&s->handle,TIM_IT_CC1 << (channel-1U));
    TIM_CCxChannelCmd(s->handle.Instance,channel_value(channel),TIM_CCx_ENABLE);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCapture_Read(unsigned id, unsigned channel, uint32_t *counter, uint8_t *overcapture)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel) || !counter || !overcapture) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->capture_active & bit)) return ABOX_HAL_INVALID;
    if (s->capture_events[channel-1U] || (s->capture_dma & bit)) return ABOX_HAL_BUSY;
    return capture_read(s,channel,counter,overcapture);
}
ABoxHalStatus ABoxCapture_Stop(unsigned id, unsigned channel)
{
    TimerInstance *s; uint8_t bit;
    if (!ABoxTimer_PwmValid(id,channel)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; bit=(uint8_t)(1U<<(channel-1U));
    if (!s->open || !(s->capture_active & bit)) return ABOX_HAL_INVALID;
    if (s->capture_dma & bit) return ABOX_HAL_BUSY;
    __HAL_TIM_DISABLE_IT(&s->handle,TIM_IT_CC1 << (channel-1U));
    TIM_CCxChannelCmd(s->handle.Instance,channel_value(channel),TIM_CCx_DISABLE);
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
    TimerInstance *s; TIM_Encoder_InitTypeDef enc={0};
    if (!ABoxEncoder_ConfigValid(id,config)) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (s->running || s->configured || s->capture_configured || s->encoder || s->slave_mode || s->hall ||
        (s->handle.Instance->CR1 & TIM_CR1_OPM)) return ABOX_HAL_BUSY;
    if (s->config.mode != ABOX_TIMER_UP) return ABOX_HAL_UNSUPPORTED;
    enc.EncoderMode=config->mode==ABOX_ENCODER_MODE1 ? TIM_ENCODERMODE_TI1 :
        config->mode==ABOX_ENCODER_MODE2 ? TIM_ENCODERMODE_TI2 : TIM_ENCODERMODE_TI12;
    enc.IC1Polarity=config->edge_a==ABOX_CAPTURE_RISING ? TIM_ENCODERINPUTPOLARITY_RISING : TIM_ENCODERINPUTPOLARITY_FALLING;
    enc.IC2Polarity=config->edge_b==ABOX_CAPTURE_RISING ? TIM_ENCODERINPUTPOLARITY_RISING : TIM_ENCODERINPUTPOLARITY_FALLING;
    enc.IC1Selection=TIM_ICSELECTION_DIRECTTI; enc.IC2Selection=TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler=TIM_ICPSC_DIV1; enc.IC2Prescaler=TIM_ICPSC_DIV1;
    enc.IC1Filter=config->filter_a; enc.IC2Filter=config->filter_b;
    if (HAL_TIM_Encoder_Init(&s->handle,&enc)!=HAL_OK) return ABOX_HAL_ERROR;
    s->encoder=1U; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxEncoder_Release(unsigned id)
{
    TimerInstance *s;
    if (id<1U || id>5U) return ABOX_HAL_INVALID;
    s=&g_timer[id-1U]; if (!s->open || !s->encoder) return ABOX_HAL_INVALID;
    if (s->running) return ABOX_HAL_BUSY;
    s->handle.Instance->SMCR &= ~(TIM_SMCR_SMS | TIM_SMCR_ECE);
    s->encoder=0U; return ABOX_HAL_OK;
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
{ return (uintptr_t)&g_timer[id-1U].handle.Instance->CCR1 + (channel-1U)*sizeof(uint32_t); }
void ABoxCaptureHw_DmaEnable(unsigned id,unsigned channel)
{
    TimerInstance *s=&g_timer[id-1U];
    __HAL_TIM_CLEAR_FLAG(&s->handle,(TIM_FLAG_CC1|TIM_FLAG_CC1OF)<<(channel-1U));
    s->handle.Instance->DIER|=TIM_DMA_CC1<<(channel-1U);
    TIM_CCxChannelCmd(s->handle.Instance,channel_value(channel),TIM_CCx_ENABLE);
}
void ABoxCaptureHw_DmaPause(unsigned id,unsigned channel)
{
    TimerInstance *s=&g_timer[id-1U];s->handle.Instance->DIER &= ~(TIM_DMA_CC1<<(channel-1U));
    TIM_CCxChannelCmd(s->handle.Instance,channel_value(channel),TIM_CCx_DISABLE);
}
int ABoxCaptureHw_DmaOvercapture(unsigned id,unsigned channel)
{ return __HAL_TIM_GET_FLAG(&g_timer[id-1U].handle,TIM_FLAG_CC1OF<<(channel-1U))!=0U; }
void ABoxCaptureHw_DmaRelease(unsigned id,unsigned channel)
{
    TimerInstance *s=&g_timer[id-1U];uint8_t bit=(uint8_t)(1U<<(channel-1U));
    ABoxCaptureHw_DmaPause(id,channel);
    __HAL_TIM_CLEAR_FLAG(&s->handle,(TIM_FLAG_CC1|TIM_FLAG_CC1OF)<<(channel-1U));
    s->capture_active&=(uint8_t)~bit;s->capture_dma&=(uint8_t)~bit;
}
