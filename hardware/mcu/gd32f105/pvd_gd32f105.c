#include "abox_pvd_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_pmu.h"
#include "gd32f10x_exti.h"
enum { PVD_LINE=1U<<16 };
void ABoxPvdHw_EnableClock(void) { rcu_periph_clock_enable(RCU_PMU); }
int ABoxPvdHw_IsEnabled(void) { return (PMU_CTL&PMU_CTL_LVDEN)!=0U; }
void ABoxPvdHw_Configure(const ABoxPvdConfig *config)
{
    EXTI_INTEN&=~PVD_LINE;EXTI_EVEN&=~PVD_LINE;
    if(config->edge==ABOX_PVD_FALLING)EXTI_RTEN&=~PVD_LINE;else EXTI_RTEN|=PVD_LINE;
    if(config->edge==ABOX_PVD_RISING)EXTI_FTEN&=~PVD_LINE;else EXTI_FTEN|=PVD_LINE;
    EXTI_PD=PVD_LINE;
    PMU_CTL=(PMU_CTL&~PMU_CTL_LVDT)|((uint32_t)config->level<<5U)|PMU_CTL_LVDEN;
    if(config->event)EXTI_EVEN|=PVD_LINE;
    if(config->interrupt)EXTI_INTEN|=PVD_LINE;
}
void ABoxPvdHw_Disable(void)
{ EXTI_INTEN&=~PVD_LINE;EXTI_EVEN&=~PVD_LINE;EXTI_RTEN&=~PVD_LINE;EXTI_FTEN&=~PVD_LINE;PMU_CTL&=~PMU_CTL_LVDEN;EXTI_PD=PVD_LINE; }
uint8_t ABoxPvdHw_Read(void) { return (PMU_CS&PMU_CS_LVDF)!=0U; }
uint8_t ABoxPvdHw_Pending(void) { return (EXTI_PD&PVD_LINE)!=0U; }
void ABoxPvdHw_Clear(void) { EXTI_PD=PVD_LINE; }
