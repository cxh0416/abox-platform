#include "abox_pvd_backend.h"
#include "stm32f1xx_hal.h"
enum { PVD_LINE=1U<<16 };
void ABoxPvdHw_EnableClock(void) { __HAL_RCC_PWR_CLK_ENABLE(); }
int ABoxPvdHw_IsEnabled(void) { return (PWR->CR&PWR_CR_PVDE)!=0U; }
void ABoxPvdHw_Configure(const ABoxPvdConfig *config)
{
    EXTI->IMR&=~PVD_LINE;EXTI->EMR&=~PVD_LINE;
    if(config->edge==ABOX_PVD_FALLING)EXTI->RTSR&=~PVD_LINE;else EXTI->RTSR|=PVD_LINE;
    if(config->edge==ABOX_PVD_RISING)EXTI->FTSR&=~PVD_LINE;else EXTI->FTSR|=PVD_LINE;
    EXTI->PR=PVD_LINE;
    PWR->CR=(PWR->CR&~PWR_CR_PLS)|((uint32_t)config->level<<5U)|PWR_CR_PVDE;
    if(config->event)EXTI->EMR|=PVD_LINE;
    if(config->interrupt)EXTI->IMR|=PVD_LINE;
}
void ABoxPvdHw_Disable(void)
{ EXTI->IMR&=~PVD_LINE;EXTI->EMR&=~PVD_LINE;EXTI->RTSR&=~PVD_LINE;EXTI->FTSR&=~PVD_LINE;PWR->CR&=~PWR_CR_PVDE;EXTI->PR=PVD_LINE; }
uint8_t ABoxPvdHw_Read(void) { return (PWR->CSR&PWR_CSR_PVDO)!=0U; }
uint8_t ABoxPvdHw_Pending(void) { return (EXTI->PR&PVD_LINE)!=0U; }
void ABoxPvdHw_Clear(void) { EXTI->PR=PVD_LINE; }
