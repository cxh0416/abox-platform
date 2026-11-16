#include "abox_clock_backend.h"
static int ahb_divider(unsigned value)
{ return value==1U||value==2U||value==4U||value==8U||value==16U||value==64U||value==128U||value==256U||value==512U; }
static int apb_divider(unsigned value)
{ return value==1U||value==2U||value==4U||value==8U||value==16U; }
ABoxHalStatus ABoxMcuClock_CalculateFor(ABoxMcuClockVariant variant,const ABoxMcuClockConfig *c,ABoxMcuClockFrequencies *out)
{
    ABoxMcuClockFrequencies f;
    uint32_t source_hz,max_sys,max_apb1;uint64_t numerator,denominator,product;int external,bypass,use_pll,cascade;
    if ((unsigned)variant>ABOX_MCU_CLOCK_GD32F105 || !c || !out || (unsigned)c->source > ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS || !ahb_divider(c->ahb_divider) ||
        !apb_divider(c->apb1_divider) || !apb_divider(c->apb2_divider)) return ABOX_HAL_INVALID;
    max_sys=variant==ABOX_MCU_CLOCK_GD32F105 ? 108000000U : 72000000U;
    max_apb1=max_sys/2U;
    cascade=c->source==ABOX_MCU_CLOCK_PLL_AUX_HSE || c->source==ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS;
    bypass=c->source==ABOX_MCU_CLOCK_HSE_BYPASS || c->source==ABOX_MCU_CLOCK_PLL_HSE_BYPASS || c->source==ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS;
    external=bypass || cascade || c->source==ABOX_MCU_CLOCK_HSE || c->source==ABOX_MCU_CLOCK_PLL_HSE;
    use_pll=cascade || c->source==ABOX_MCU_CLOCK_PLL_HSI || c->source==ABOX_MCU_CLOCK_PLL_HSE || c->source==ABOX_MCU_CLOCK_PLL_HSE_BYPASS;
    if (external && (c->external_hz < (bypass ? 1000000U : 3000000U) ||
                     c->external_hz > (bypass ? 50000000U : 25000000U))) return ABOX_HAL_INVALID;
    if (use_pll) {
        if (c->pll_multiplier!=ABOX_MCU_CLOCK_PLL_MUL_6_5 &&
            (variant==ABOX_MCU_CLOCK_STM32F105 ? (c->pll_multiplier<4U || c->pll_multiplier>9U) :
             (c->pll_multiplier<2U || c->pll_multiplier>32U || c->pll_multiplier==15U))) return ABOX_HAL_UNSUPPORTED;
        if (c->source == ABOX_MCU_CLOCK_PLL_HSI) { numerator=4000000U;denominator=1U; }
        else {
            if (!c->external_divider || c->external_divider > 16U) return ABOX_HAL_INVALID;
            numerator=c->external_hz;denominator=c->external_divider;
            if (cascade) {
                if (!c->aux_divider || c->aux_divider>16U) return ABOX_HAL_INVALID;
                if (!((c->aux_multiplier>=8U && c->aux_multiplier<=14U) || c->aux_multiplier==16U ||
                      c->aux_multiplier==20U || (variant==ABOX_MCU_CLOCK_GD32F105 && c->aux_multiplier==15U))) return ABOX_HAL_UNSUPPORTED;
                product=(uint64_t)c->external_hz*c->aux_multiplier;
                if ((uint64_t)c->external_hz<(variant==ABOX_MCU_CLOCK_GD32F105 ? 1000000ULL : 3000000ULL)*c->aux_divider ||
                    (uint64_t)c->external_hz>(variant==ABOX_MCU_CLOCK_GD32F105 ? 25000000ULL : 5000000ULL)*c->aux_divider ||
                    product<(variant==ABOX_MCU_CLOCK_GD32F105 ? 16000000ULL : 40000000ULL)*c->aux_divider ||
                    product>(variant==ABOX_MCU_CLOCK_GD32F105 ? 108000000ULL : 74000000ULL)*c->aux_divider) return ABOX_HAL_UNSUPPORTED;
                numerator=product;denominator*=c->aux_divider;
            }
        }
        if (numerator < (variant==ABOX_MCU_CLOCK_GD32F105 ? 1000000ULL : 3000000ULL)*denominator ||
            numerator > (variant==ABOX_MCU_CLOCK_GD32F105 ? 25000000ULL : 12000000ULL)*denominator) return ABOX_HAL_UNSUPPORTED;
        product=numerator*(c->pll_multiplier==ABOX_MCU_CLOCK_PLL_MUL_6_5 ? 13U : c->pll_multiplier);
        if(c->pll_multiplier==ABOX_MCU_CLOCK_PLL_MUL_6_5) denominator*=2U;
        if(product < (variant==ABOX_MCU_CLOCK_GD32F105 ? 16000000ULL : 18000000ULL)*denominator ||
           product>(uint64_t)max_sys*denominator) return ABOX_HAL_UNSUPPORTED;
        if (product>(uint64_t)max_apb1*denominator*c->ahb_divider*c->apb1_divider) return ABOX_HAL_UNSUPPORTED;
        source_hz=(uint32_t)(product/denominator);
    } else source_hz = external ? c->external_hz : 8000000U;
    f.system_hz = source_hz; f.ahb_hz = source_hz / c->ahb_divider;
    f.apb1_hz = f.ahb_hz / c->apb1_divider; f.apb2_hz = f.ahb_hz / c->apb2_divider;
    if (!f.ahb_hz || !f.apb1_hz || !f.apb2_hz || f.ahb_hz > max_sys ||
        f.apb1_hz > max_apb1 || f.apb2_hz > max_sys) return ABOX_HAL_UNSUPPORTED;
    f.apb1_timer_hz = f.apb1_hz * (c->apb1_divider == 1U ? 1U : 2U);
    f.apb2_timer_hz = f.apb2_hz * (c->apb2_divider == 1U ? 1U : 2U);
    *out = f;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClock_Calculate(const ABoxMcuClockConfig *c,ABoxMcuClockFrequencies *out)
{
#ifdef ABOX_MCU_GD32F105
    return ABoxMcuClock_CalculateFor(ABOX_MCU_CLOCK_GD32F105,c,out);
#else
    return ABoxMcuClock_CalculateFor(ABOX_MCU_CLOCK_STM32F105,c,out);
#endif
}
ABoxHalStatus ABoxMcuClock_Apply(const ABoxMcuClockConfig *config)
{
    ABoxMcuClockFrequencies f;
    ABoxHalStatus status = ABoxMcuClock_Calculate(config, &f);
    return status == ABOX_HAL_OK ? ABoxMcuClockHw_Apply(config, &f) : status;
}
ABoxHalStatus ABoxMcuClock_Get(ABoxMcuClockFrequencies *out)
{ return out ? ABoxMcuClockHw_Get(out) : ABOX_HAL_INVALID; }
