#include <assert.h>
#include <stdint.h>
#include "abox_clock.h" /* UTC synchronization clock must coexist with MCU clock. */
#include "abox_clock_backend.h"
static unsigned calls;
static ABoxHalStatus backend_result=ABOX_HAL_OK;
ABoxHalStatus ABoxMcuClockHw_Apply(const ABoxMcuClockConfig *c,const ABoxMcuClockFrequencies *expected)
{ assert(c && expected);++calls;return backend_result; }
ABoxHalStatus ABoxMcuClockHw_Get(ABoxMcuClockFrequencies *out)
{ out->system_hz=123U;return ABOX_HAL_OK; }
int main(void)
{
    ABoxMcuClockConfig c={ABOX_MCU_CLOCK_PLL_HSE,8000000U,1U,9U,1U,2U,1U,0U,0U};
    ABoxMcuClockFrequencies f={0};
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK);
    assert(f.system_hz==72000000U && f.ahb_hz==72000000U && f.apb1_hz==36000000U);
    assert(f.apb2_hz==72000000U && f.apb1_timer_hz==72000000U && f.apb2_timer_hz==72000000U);
    assert(ABoxMcuClock_Apply(&c)==ABOX_HAL_OK && calls==1U);
    c.apb1_divider=1U;f.system_hz=123U;
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_UNSUPPORTED && f.system_hz==123U);
    assert(ABoxMcuClock_Apply(&c)==ABOX_HAL_UNSUPPORTED && calls==1U);c.apb1_divider=2U;
    c.external_divider=2U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==36000000U && f.apb1_hz==18000000U);
    c.external_divider=3U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_UNSUPPORTED);c.external_divider=1U;
    c.pll_multiplier=10U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_UNSUPPORTED);c.pll_multiplier=9U;
    c.source=ABOX_MCU_CLOCK_PLL_HSI;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==36000000U);
    c.pll_multiplier=4U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_UNSUPPORTED);
    c.source=ABOX_MCU_CLOCK_HSI;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==8000000U);
    c.source=ABOX_MCU_CLOCK_HSE;c.external_hz=25000000U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==25000000U);
    c.external_hz=25000001U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_INVALID);
    c.source=ABOX_MCU_CLOCK_HSE_BYPASS;c.external_hz=50000000U;
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==50000000U);
    c.external_hz=50000001U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_INVALID);
    c.external_hz=1000000U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==1000000U);
    c.external_hz=999999U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_INVALID);
    c.source=ABOX_MCU_CLOCK_PLL_HSE_BYPASS;c.external_hz=50000000U;c.external_divider=5U;c.pll_multiplier=7U;
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==70000000U);
    c.source=ABOX_MCU_CLOCK_PLL_AUX_HSE;c.external_hz=8000000U;c.external_divider=8U;c.pll_multiplier=9U;
    c.aux_divider=2U;c.aux_multiplier=16U;
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==72000000U);
    c.aux_multiplier=15U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_UNSUPPORTED);
    c.external_divider=7U;assert(ABoxMcuClock_CalculateFor(ABOX_MCU_CLOCK_GD32F105,&c,&f)==ABOX_HAL_OK && f.system_hz==77142857U);
    c.external_divider=10U;assert(ABoxMcuClock_CalculateFor(ABOX_MCU_CLOCK_GD32F105,&c,&f)==ABOX_HAL_OK && f.system_hz==54000000U);
    c.external_hz=8000000U;c.ahb_divider=3U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_INVALID);
    assert(ABoxMcuClock_Get(0)==ABOX_HAL_INVALID && ABoxMcuClock_Get(&f)==ABOX_HAL_OK && f.system_hz==123U);
    return 0;
}
