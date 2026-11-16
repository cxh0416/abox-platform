#include "abox_low_power_backend.h"
#if defined(ABOX_MCU_GD32F105)
#include "gd32f10x.h"
#include "gd32f10x_pmu.h"
#define POWER_CTL PMU_CTL
#define POWER_STATUS PMU_CS
#define POWER_CLOCK() rcu_periph_clock_enable(RCU_PMU)
#else
#include "stm32f1xx_hal.h"
#define POWER_CTL (PWR->CR)
#define POWER_STATUS (PWR->CSR)
#define POWER_CLOCK() __HAL_RCC_PWR_CLK_ENABLE()
#endif
int ABoxLowPowerHw_CanEnter(void)
{ return !__get_IPSR() && !__get_PRIMASK() && !__get_BASEPRI(); }
int ABoxLowPowerHw_OnWakeClock(void)
{
#if defined(ABOX_MCU_GD32F105)
    return rcu_system_clock_source_get()==RCU_SCSS_IRC8M && !(RCU_CTL & RCU_CTL_PLLEN);
#else
    return __HAL_RCC_GET_SYSCLK_SOURCE()==RCC_SYSCLKSOURCE_STATUS_HSI && !(RCC->CR & RCC_CR_PLLON);
#endif
}
int ABoxLowPowerHw_SourceMatches(ABoxMcuClockSource source)
{
#if defined(ABOX_MCU_GD32F105)
    uint32_t current=rcu_system_clock_source_get();
    if (source==ABOX_MCU_CLOCK_HSI) return current==RCU_SCSS_IRC8M;
    if (source==ABOX_MCU_CLOCK_HSE) return current==RCU_SCSS_HXTAL;
    if (current!=RCU_SCSS_PLL) return 0;
    return source==ABOX_MCU_CLOCK_PLL_HSE ? !!(RCU_CFG0 & RCU_CFG0_PLLSEL) : !(RCU_CFG0 & RCU_CFG0_PLLSEL);
#else
    uint32_t current=__HAL_RCC_GET_SYSCLK_SOURCE();
    if (source==ABOX_MCU_CLOCK_HSI) return current==RCC_SYSCLKSOURCE_STATUS_HSI;
    if (source==ABOX_MCU_CLOCK_HSE) return current==RCC_SYSCLKSOURCE_STATUS_HSE;
    if (current!=RCC_SYSCLKSOURCE_STATUS_PLLCLK) return 0;
    return source==ABOX_MCU_CLOCK_PLL_HSE ? !!(RCC->CFGR & RCC_CFGR_PLLSRC) : !(RCC->CFGR & RCC_CFGR_PLLSRC);
#endif
}
void ABoxLowPowerHw_Enter(ABoxLowPowerMode mode,ABoxLowPowerWait wait,uint8_t low_regulator)
{
    uint32_t old_scr=SCB->SCR;
    POWER_CLOCK();
    if (mode==ABOX_LOW_POWER_SLEEP) SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    else {
        POWER_CTL=(POWER_CTL & ~3U) | (mode==ABOX_LOW_POWER_STANDBY ? 2U : (uint32_t)low_regulator);
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    }
    __DSB();
    if (wait==ABOX_LOW_POWER_WFI) __WFI();
    else { __SEV();__WFE();__WFE(); }
    __ISB();SCB->SCR=old_scr;
}
uint8_t ABoxLowPowerHw_Flags(void) { POWER_CLOCK();return (uint8_t)(POWER_STATUS & 3U); }
void ABoxLowPowerHw_ClearFlags(uint8_t flags)
{ POWER_CLOCK();POWER_CTL |= ((uint32_t)flags<<2U); }
void ABoxLowPowerHw_SetWakePin(uint8_t enabled)
{ POWER_CLOCK();if (enabled) POWER_STATUS |= 1U<<8U; else POWER_STATUS &= ~(1U<<8U); }
