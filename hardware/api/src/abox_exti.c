#include "abox_exti_backend.h"

typedef struct { ABoxGpioPin pin; ABoxExtiEvents events; uint8_t active,routes; } ExtiSlot;
static ExtiSlot slots[16];
typedef struct { ABoxExtiInternalEvents events;uint8_t routes,active; } InternalSlot;
static InternalSlot internal[3];

ABoxHalStatus ABoxExti_Open(ABoxGpioPin pin, ABoxExtiEdge edge, const ABoxExtiEvents *events)
{ ABoxExtiConfig config={edge,ABOX_EXTI_INTERRUPT};return ABoxExti_OpenConfigured(pin,&config,events); }
ABoxHalStatus ABoxExti_OpenConfigured(ABoxGpioPin pin,const ABoxExtiConfig *config,const ABoxExtiEvents *events)
{
    ExtiSlot *s;
    if ((unsigned)pin.port > ABOX_GPIO_E || pin.index >= 16U ||
        !config || (unsigned)config->edge > ABOX_EXTI_BOTH || !config->routes || (config->routes & ~3U) ||
        ((config->routes & ABOX_EXTI_INTERRUPT) && (!events || !events->on_edge))) return ABOX_HAL_INVALID;
    s = &slots[pin.index];
    if (s->active) return ABOX_HAL_BUSY;
    s->pin = pin; if(events)s->events = *events;else { s->events.context=0;s->events.on_edge=0; }
    s->routes=config->routes;s->active = 1U;
    ABoxExtiHw_Open(pin, config->edge,config->routes);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxExti_Close(ABoxGpioPin pin)
{
    ExtiSlot *s;
    if ((unsigned)pin.port > ABOX_GPIO_E || pin.index >= 16U) return ABOX_HAL_INVALID;
    s = &slots[pin.index];
    if (!s->active || s->pin.port != pin.port) return ABOX_HAL_INVALID;
    ABoxExtiHw_Close(pin.index);
    s->active = s->routes = 0U; s->events.on_edge = 0; s->events.context = 0;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxExti_Trigger(ABoxGpioPin pin)
{
    ExtiSlot *s;
    if((unsigned)pin.port>ABOX_GPIO_E || pin.index>=16U)return ABOX_HAL_INVALID;
    s=&slots[pin.index];if(!s->active || s->pin.port!=pin.port)return ABOX_HAL_INVALID;
    if(!(s->routes & ABOX_EXTI_INTERRUPT))return ABOX_HAL_UNSUPPORTED;
    ABoxExtiHw_Trigger(pin.index);return ABOX_HAL_OK;
}
void ABoxExti_Irq(uint16_t lines)
{
    uint16_t pending = ABoxExtiHw_Pending(lines);
    unsigned line;
    for (line = 0U; line < 16U; ++line) {
        if ((pending & (uint16_t)(1U << line)) && slots[line].active && (slots[line].routes & ABOX_EXTI_INTERRUPT)) {
            ABoxExtiHw_Clear(line);
            slots[line].events.on_edge(slots[line].events.context, slots[line].pin);
        }
    }
}
ABoxHalStatus ABoxExtiInternal_Open(unsigned line,const ABoxExtiConfig *config,const ABoxExtiInternalEvents *events)
{
    InternalSlot *s;
    if(line<16U || line>18U || !config || (unsigned)config->edge>ABOX_EXTI_BOTH ||
       !config->routes || (config->routes & ~3U) ||
       ((config->routes & ABOX_EXTI_INTERRUPT) && (!events || !events->on_event)))return ABOX_HAL_INVALID;
    s=&internal[line-16U];if(s->active)return ABOX_HAL_BUSY;
    if(events)s->events=*events;else { s->events.context=0;s->events.on_event=0; }
    s->routes=config->routes;s->active=1U;ABoxExtiHw_OpenInternal(line,config->edge,config->routes);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxExtiInternal_Close(unsigned line)
{
    InternalSlot *s;
    if(line<16U || line>18U)return ABOX_HAL_INVALID;
    s=&internal[line-16U];if(!s->active)return ABOX_HAL_INVALID;
    ABoxExtiHw_Close(line);s->active=s->routes=0U;s->events.context=0;s->events.on_event=0;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxExtiInternal_Trigger(unsigned line)
{
    if(line<16U || line>18U || !internal[line-16U].active)return ABOX_HAL_INVALID;
    if(!(internal[line-16U].routes & ABOX_EXTI_INTERRUPT))return ABOX_HAL_UNSUPPORTED;
    ABoxExtiHw_Trigger(line);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxExtiInternal_Pending(unsigned line,uint8_t *pending)
{
    if(line<16U || line>18U || !pending || !internal[line-16U].active)return ABOX_HAL_INVALID;
    *pending=(uint8_t)!!ABoxExtiHw_RawPending(1UL<<line);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxExtiInternal_Clear(unsigned line)
{
    if(line<16U || line>18U || !internal[line-16U].active)return ABOX_HAL_INVALID;
    ABoxExtiHw_Clear(line);return ABOX_HAL_OK;
}
void ABoxExtiInternal_Irq(uint32_t lines)
{
    uint32_t pending=ABoxExtiHw_EnabledPending(lines & 0x70000U);
    for(unsigned line=16U;line<=18U;++line) {
        InternalSlot *s=&internal[line-16U];
        if((pending & (1UL<<line)) && s->active && (s->routes & ABOX_EXTI_INTERRUPT)) {
            ABoxExtiHw_Clear(line);s->events.on_event(s->events.context,line);
        }
    }
}
