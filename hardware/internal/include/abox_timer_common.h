#ifndef ABOX_TIMER_COMMON_H
#define ABOX_TIMER_COMMON_H
#include "abox_timer.h"
static inline int ABoxTimer_ConfigValid(unsigned id, const ABoxTimerConfig *c)
{
    return id >= 1U && id <= 7U && c && c->prescaler >= 1U && c->prescaler <= 65536U &&
           c->period >= 1U && c->period <= 65536U && (unsigned)c->mode <= ABOX_TIMER_CENTER_BOTH &&
           (id <= 5U || c->mode == ABOX_TIMER_UP);
}
static inline int ABoxTimer_PwmValid(unsigned id, unsigned channel)
{ return id >= 1U && id <= 5U && channel >= 1U && channel <= 4U; }
static inline int ABoxCapture_ConfigValid(unsigned id, unsigned channel, const ABoxCaptureConfig *c)
{
    return ABoxTimer_PwmValid(id, channel) && c && (unsigned)c->edge <= ABOX_CAPTURE_FALLING &&
        (unsigned)c->input <= ABOX_CAPTURE_TRIGGER && c->filter <= 15U &&
        (c->prescaler == 1U || c->prescaler == 2U || c->prescaler == 4U || c->prescaler == 8U);
}
static inline int ABoxEncoder_ConfigValid(unsigned id, const ABoxEncoderConfig *c)
{
    return id >= 1U && id <= 5U && c && (unsigned)c->mode <= ABOX_ENCODER_BOTH &&
        (unsigned)c->edge_a <= ABOX_CAPTURE_FALLING && (unsigned)c->edge_b <= ABOX_CAPTURE_FALLING &&
        c->filter_a <= 15U && c->filter_b <= 15U;
}
static inline int ABoxTimer_BreakConfigValid(const ABoxTimerBreakConfig *c)
{
    return c && c->break_enable<=1U && c->break_active_high<=1U && c->auto_rearm<=1U &&
           c->off_state_idle<=1U && c->off_state_run<=1U;
}
static inline int ABoxTimer_SlaveConfigValid(unsigned id,const ABoxTimerSlaveConfig *c)
{
    return id>=1U && id<=5U && c &&
           (c->mode==ABOX_TIMER_SLAVE_DISABLED || (c->mode>=ABOX_TIMER_SLAVE_RESET && c->mode<=ABOX_TIMER_EXTERNAL_CLOCK_2)) &&
           (unsigned)c->source<=ABOX_TIMER_ETR && c->etr_filter<=15U && c->etr_inverted<=1U &&
           (c->etr_prescaler==1U || c->etr_prescaler==2U || c->etr_prescaler==4U || c->etr_prescaler==8U) &&
           !(c->mode==ABOX_TIMER_SLAVE_GATED && c->source==ABOX_TIMER_TI1_EDGE);
}
static inline uint32_t ABoxTimer_SlaveBits(const ABoxTimerSlaveConfig *c)
{
    unsigned prescale=0U;uint8_t n=c->etr_prescaler;
    if (c->mode==ABOX_TIMER_SLAVE_DISABLED) return 0U;
    while (n>1U) { ++prescale;n>>=1U; }
    return ((uint32_t)c->source<<4U) | ((uint32_t)c->etr_filter<<8U) | ((uint32_t)prescale<<12U) |
           ((uint32_t)c->etr_inverted<<15U) |
           (c->mode==ABOX_TIMER_EXTERNAL_CLOCK_2 ? (1U<<14U) : (uint32_t)c->mode);
}
static inline uint32_t ABoxTimer_BreakBits(const ABoxTimerBreakConfig *c)
{
    return (uint32_t)c->dead_time_code | ((uint32_t)c->off_state_idle<<10U) |
           ((uint32_t)c->off_state_run<<11U) | ((uint32_t)c->break_enable<<12U) |
           ((uint32_t)c->break_active_high<<13U) | ((uint32_t)c->auto_rearm<<14U);
}
#endif
