#include "abox_exti_backend.h"
#include "stm32f1xx_hal.h"

void ABoxExtiHw_OpenInternal(unsigned line,ABoxExtiEdge edge,uint8_t routes)
{
    uint32_t mask=1UL<<line;
    EXTI->IMR &= ~mask;EXTI->EMR &= ~mask;
    if(edge==ABOX_EXTI_RISING || edge==ABOX_EXTI_BOTH)EXTI->RTSR|=mask;else EXTI->RTSR &= ~mask;
    if(edge==ABOX_EXTI_FALLING || edge==ABOX_EXTI_BOTH)EXTI->FTSR|=mask;else EXTI->FTSR &= ~mask;
    EXTI->PR=mask;
    if(routes & ABOX_EXTI_EVENT)EXTI->EMR|=mask;
    if(routes & ABOX_EXTI_INTERRUPT)EXTI->IMR|=mask;
}
void ABoxExtiHw_Open(ABoxGpioPin pin, ABoxExtiEdge edge,uint8_t routes)
{
    uint32_t mask = 1UL << pin.index;
    uint32_t shift = 4U * (pin.index & 3U);
    uint32_t index = pin.index >> 2U;
    __HAL_RCC_AFIO_CLK_ENABLE();
    EXTI->IMR &= ~mask;EXTI->EMR &= ~mask;
    AFIO->EXTICR[index] = (AFIO->EXTICR[index] & ~(15UL << shift)) | ((uint32_t)pin.port << shift);
    ABoxExtiHw_OpenInternal(pin.index,edge,routes);
}
void ABoxExtiHw_Close(unsigned line)
{
    uint32_t mask = 1UL << line;
    EXTI->IMR &= ~mask;
    EXTI->EMR &= ~mask;
    EXTI->RTSR &= ~mask; EXTI->FTSR &= ~mask;
    EXTI->PR = mask;
}
uint16_t ABoxExtiHw_Pending(uint16_t lines) { return (uint16_t)(EXTI->PR & EXTI->IMR & lines); }
void ABoxExtiHw_Clear(unsigned line) { EXTI->PR = 1UL << line; }
void ABoxExtiHw_Trigger(unsigned line) { EXTI->SWIER |= 1UL << line; }
uint32_t ABoxExtiHw_RawPending(uint32_t lines) { return EXTI->PR & lines; }
uint32_t ABoxExtiHw_EnabledPending(uint32_t lines) { return EXTI->PR & EXTI->IMR & lines; }
