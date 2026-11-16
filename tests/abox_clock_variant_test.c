#include <assert.h>
#include "abox_clock_backend.h"
static unsigned applied;
ABoxHalStatus ABoxMcuClockHw_Apply(const ABoxMcuClockConfig *c,const ABoxMcuClockFrequencies *f)
{ (void)c;assert(f->system_hz==108000000U && f->apb1_hz==54000000U);++applied;return ABOX_HAL_OK; }
ABoxHalStatus ABoxMcuClockHw_Get(ABoxMcuClockFrequencies *f) { (void)f;return ABOX_HAL_OK; }
int main(void)
{
    ABoxMcuClockConfig c={ABOX_MCU_CLOCK_PLL_HSI,0U,0U,27U,1U,2U,1U,0U,0U};ABoxMcuClockFrequencies f={0};
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==108000000U && f.apb1_hz==54000000U);
    assert(ABoxMcuClock_Apply(&c)==ABOX_HAL_OK && applied==1U);
    assert(ABoxMcuClock_CalculateFor(ABOX_MCU_CLOCK_STM32F105,&c,&f)==ABOX_HAL_UNSUPPORTED);
    c.pll_multiplier=15U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_UNSUPPORTED);
    c.pll_multiplier=28U;assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_UNSUPPORTED);
    c.pll_multiplier=ABOX_MCU_CLOCK_PLL_MUL_6_5;
    assert(ABoxMcuClock_CalculateFor(ABOX_MCU_CLOCK_STM32F105,&c,&f)==ABOX_HAL_OK && f.system_hz==26000000U);
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==26000000U);
    c.source=ABOX_MCU_CLOCK_PLL_HSE;c.external_hz=3000000U;c.external_divider=3U;c.pll_multiplier=16U;
    assert(ABoxMcuClock_Calculate(&c,&f)==ABOX_HAL_OK && f.system_hz==16000000U);
    assert(ABoxMcuClock_CalculateFor(ABOX_MCU_CLOCK_STM32F105,&c,&f)==ABOX_HAL_UNSUPPORTED);
    assert(ABoxMcuClock_CalculateFor((ABoxMcuClockVariant)2,&c,&f)==ABOX_HAL_INVALID);
    return 0;
}
