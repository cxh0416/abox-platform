#include <assert.h>
#include "abox_low_power_backend.h"
#include "common.h"
FakeScb fake_scb;uint32_t fake_power[2],fake_ipsr,fake_primask,fake_basepri,sleep_scr,sleep_ctl;
uint32_t fake_rcc_regs[2],fake_rcc_source;
unsigned fake_waits,fake_events,fake_clocks;
int main(void)
{
    assert(ABoxLowPowerHw_CanEnter());fake_ipsr=1U;assert(!ABoxLowPowerHw_CanEnter());fake_ipsr=0U;
    assert(ABoxLowPowerHw_OnWakeClock());fake_rcc_source=1U;assert(!ABoxLowPowerHw_OnWakeClock());fake_rcc_source=0U;
    assert(ABoxLowPowerHw_SourceMatches(ABOX_MCU_CLOCK_HSI));
    assert(!ABoxLowPowerHw_SourceMatches(ABOX_MCU_CLOCK_HSE));
    fake_rcc_source=2U;assert(ABoxLowPowerHw_SourceMatches(ABOX_MCU_CLOCK_PLL_HSI));
    assert(!ABoxLowPowerHw_SourceMatches(ABOX_MCU_CLOCK_PLL_HSE));
    fake_rcc_cfg=0x10000U;assert(ABoxLowPowerHw_SourceMatches(ABOX_MCU_CLOCK_PLL_HSE));
    assert(!ABoxLowPowerHw_SourceMatches(ABOX_MCU_CLOCK_PLL_HSI));fake_rcc_cfg=0U;fake_rcc_source=0U;
    fake_rcc_ctl=0x100U;assert(!ABoxLowPowerHw_OnWakeClock());fake_rcc_ctl=0U;
    fake_primask=1U;assert(!ABoxLowPowerHw_CanEnter());fake_primask=0U;
    fake_basepri=1U;assert(!ABoxLowPowerHw_CanEnter());fake_basepri=0U;
    fake_scb.SCR=2U;fake_power[0]=0x100U;
    ABoxLowPowerHw_Enter(ABOX_LOW_POWER_STOP,ABOX_LOW_POWER_WFI,1U);
    assert(fake_waits==1U && sleep_scr==6U && sleep_ctl==0x101U && fake_scb.SCR==2U);
    ABoxLowPowerHw_Enter(ABOX_LOW_POWER_SLEEP,ABOX_LOW_POWER_WFE,0U);
    assert(fake_waits==3U && fake_events==1U && sleep_scr==2U && fake_scb.SCR==2U);
    ABoxLowPowerHw_Enter(ABOX_LOW_POWER_STANDBY,ABOX_LOW_POWER_WFI,0U);
    assert((sleep_ctl&3U)==2U && sleep_scr==6U);
    fake_power[1]=3U;assert(ABoxLowPowerHw_Flags()==3U);
    fake_power[0]=0x100U;ABoxLowPowerHw_ClearFlags(1U);assert(fake_power[0]==0x104U);
    ABoxLowPowerHw_SetWakePin(1U);assert(fake_power[1]&0x100U);
    ABoxLowPowerHw_SetWakePin(0U);assert(!(fake_power[1]&0x100U));
    return 0;
}
