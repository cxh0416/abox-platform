#include <assert.h>
#include "abox_exti_backend.h"
static uint32_t pending, cleared;
static unsigned opened, closed, callbacks,configured_routes,triggered,internal_callbacks;
static ABoxGpioPin configured;
static ABoxExtiEdge selected;
void ABoxExtiHw_Open(ABoxGpioPin pin, ABoxExtiEdge edge,uint8_t routes) { configured=pin; selected=edge; configured_routes=routes;++opened; }
void ABoxExtiHw_OpenInternal(unsigned line,ABoxExtiEdge edge,uint8_t routes) { assert(line>=16U && line<=18U);selected=edge;configured_routes=routes;++opened; }
void ABoxExtiHw_Close(unsigned line) { assert(line == 5U || (line>=16U && line<=18U)); ++closed; }
uint16_t ABoxExtiHw_Pending(uint16_t lines) { return pending & lines; }
uint32_t ABoxExtiHw_RawPending(uint32_t lines) { return pending & lines; }
uint32_t ABoxExtiHw_EnabledPending(uint32_t lines) { return pending & lines; }
void ABoxExtiHw_Clear(unsigned line) { cleared |= 1U << line; pending &= ~(1U << line); }
void ABoxExtiHw_Trigger(unsigned line) { triggered|=1U<<line; }
static void edge(void *context, ABoxGpioPin pin) { assert(context == &callbacks && pin.port == ABOX_GPIO_C && pin.index == 5U); ++callbacks; }
static void internal_event(void *context,unsigned line) { assert(context==&internal_callbacks && line==17U);++internal_callbacks; }
int main(void)
{
    ABoxGpioPin pin={ABOX_GPIO_C,5}, other={ABOX_GPIO_A,5}, invalid={ABOX_GPIO_A,16};
    ABoxExtiEvents events={&callbacks,edge};
    assert(ABoxExti_Open(invalid, ABOX_EXTI_BOTH, &events)==ABOX_HAL_INVALID);
    assert(ABoxExti_Open(pin, (ABoxExtiEdge)3, &events)==ABOX_HAL_INVALID);
    assert(ABoxExti_Open(pin, ABOX_EXTI_BOTH, 0)==ABOX_HAL_INVALID && !opened);
    assert(ABoxExti_Open(pin, ABOX_EXTI_BOTH, &events)==ABOX_HAL_OK && opened==1U);
    assert(configured.port==pin.port && configured.index==pin.index && selected==ABOX_EXTI_BOTH);
    assert(configured_routes==ABOX_EXTI_INTERRUPT && ABoxExti_Trigger(pin)==ABOX_HAL_OK && triggered==(1U<<5));
    assert(ABoxExti_Open(other, ABOX_EXTI_RISING, &events)==ABOX_HAL_BUSY);
    events.on_edge=0; /* API retains a copy, not a borrowed event table. */
    pending=(1U<<5)|(1U<<6); ABoxExti_Irq(1U<<6);
    assert(!callbacks && !cleared && pending==((1U<<5)|(1U<<6)));
    ABoxExti_Irq((1U<<5)|(1U<<6));
    assert(callbacks==1U && cleared==(1U<<5) && pending==(1U<<6));
    assert(ABoxExti_Close(other)==ABOX_HAL_INVALID && !closed);
    assert(ABoxExti_Close(pin)==ABOX_HAL_OK && closed==1U);
    pending=1U<<5; ABoxExti_Irq(1U<<5); assert(callbacks==1U);
    assert(ABoxExti_Close(pin)==ABOX_HAL_INVALID);
    {
        ABoxExtiConfig c={ABOX_EXTI_FALLING,ABOX_EXTI_EVENT};
        assert(ABoxExti_OpenConfigured(pin,&c,0)==ABOX_HAL_OK && configured_routes==ABOX_EXTI_EVENT);
        assert(ABoxExti_Trigger(pin)==ABOX_HAL_UNSUPPORTED);
        pending=1U<<5;ABoxExti_Irq(1U<<5);assert(callbacks==1U);
        assert(ABoxExti_Close(pin)==ABOX_HAL_OK);
        c.routes=ABOX_EXTI_EVENT|ABOX_EXTI_INTERRUPT;
        assert(ABoxExti_OpenConfigured(pin,&c,0)==ABOX_HAL_INVALID);
        events.on_edge=edge;assert(ABoxExti_OpenConfigured(pin,&c,&events)==ABOX_HAL_OK && configured_routes==3U);
        assert(ABoxExti_Close(pin)==ABOX_HAL_OK);
    }
    {
        ABoxExtiConfig c={ABOX_EXTI_RISING,ABOX_EXTI_INTERRUPT|ABOX_EXTI_EVENT};
        ABoxExtiInternalEvents e={&internal_callbacks,internal_event};uint8_t p=0U;
        assert(ABoxExtiInternal_Open(19U,&c,&e)==ABOX_HAL_INVALID);
        assert(ABoxExtiInternal_Open(17U,&c,&e)==ABOX_HAL_OK);
        assert(ABoxExtiInternal_Open(17U,&c,&e)==ABOX_HAL_BUSY);
        assert(ABoxExtiInternal_Trigger(17U)==ABOX_HAL_OK && (triggered&(1U<<17)));
        pending|=1U<<17;assert(ABoxExtiInternal_Pending(17U,&p)==ABOX_HAL_OK && p);
        ABoxExtiInternal_Irq(1U<<17);assert(internal_callbacks==1U && !(pending&(1U<<17)));
        pending|=1U<<17;assert(ABoxExtiInternal_Clear(17U)==ABOX_HAL_OK && !(pending&(1U<<17)));
        assert(ABoxExtiInternal_Close(17U)==ABOX_HAL_OK);
    }
    return 0;
}
