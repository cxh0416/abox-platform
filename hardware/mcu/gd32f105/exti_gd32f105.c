#include "abox_exti_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_exti.h"

void ABoxExtiHw_OpenInternal(unsigned line,ABoxExtiEdge edge,uint8_t routes)
{
    uint32_t mask=1UL<<line;
    EXTI_INTEN &= ~mask;EXTI_EVEN &= ~mask;
    if (edge == ABOX_EXTI_RISING || edge == ABOX_EXTI_BOTH) EXTI_RTEN |= mask; else EXTI_RTEN &= ~mask;
    if (edge == ABOX_EXTI_FALLING || edge == ABOX_EXTI_BOTH) EXTI_FTEN |= mask; else EXTI_FTEN &= ~mask;
    EXTI_PD = mask;
    if(routes & ABOX_EXTI_EVENT)EXTI_EVEN |= mask;
    if(routes & ABOX_EXTI_INTERRUPT)EXTI_INTEN |= mask;
}
void ABoxExtiHw_Open(ABoxGpioPin pin, ABoxExtiEdge edge,uint8_t routes)
{
    uint32_t mask=1UL<<pin.index;
    rcu_periph_clock_enable(RCU_AF);EXTI_INTEN &= ~mask;EXTI_EVEN &= ~mask;
    gpio_exti_source_select((uint8_t)pin.port,pin.index);
    ABoxExtiHw_OpenInternal(pin.index,edge,routes);
}
void ABoxExtiHw_Close(unsigned line)
{
    uint32_t mask = 1UL << line;
    EXTI_INTEN &= ~mask;
    EXTI_EVEN &= ~mask;
    EXTI_RTEN &= ~mask; EXTI_FTEN &= ~mask;
    EXTI_PD = mask;
}
uint16_t ABoxExtiHw_Pending(uint16_t lines) { return (uint16_t)(EXTI_PD & EXTI_INTEN & lines); }
void ABoxExtiHw_Clear(unsigned line) { EXTI_PD = 1UL << line; }
void ABoxExtiHw_Trigger(unsigned line) { EXTI_SWIEV |= 1UL << line; }
uint32_t ABoxExtiHw_RawPending(uint32_t lines) { return EXTI_PD & lines; }
uint32_t ABoxExtiHw_EnabledPending(uint32_t lines) { return EXTI_PD & EXTI_INTEN & lines; }
