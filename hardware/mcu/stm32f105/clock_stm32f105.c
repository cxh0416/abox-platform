#include "abox_clock_backend.h"
#include "stm32f1xx_hal.h"

static int output_valid(unsigned source)
{ return source==0U || (source>=4U && source<=9U) || source==11U; }
ABoxHalStatus ABoxMcuClock_SetOutput(ABoxMcuClockOutput source)
{
    if (!output_valid((unsigned)source)) return ABOX_HAL_INVALID;
    RCC->CFGR=(RCC->CFGR & ~RCC_CFGR_MCO) | ((uint32_t)source<<24U);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClock_GetOutput(ABoxMcuClockOutput *source)
{
    unsigned value;if (!source) return ABOX_HAL_INVALID;
    value=(RCC->CFGR & RCC_CFGR_MCO)>>24U;if (!output_valid(value)) return ABOX_HAL_ERROR;
    *source=(ABoxMcuClockOutput)value;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClock_SetFailureMonitor(uint8_t enabled)
{
    if (enabled>1U) return ABOX_HAL_INVALID;
    if (enabled) RCC->CR |= RCC_CR_CSSON;else RCC->CR &= ~RCC_CR_CSSON;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClock_FailurePending(uint8_t *pending)
{ if (!pending) return ABOX_HAL_INVALID;*pending=(RCC->CIR & RCC_CIR_CSSF) ? 1U : 0U;return ABOX_HAL_OK; }
void ABoxMcuClock_ClearFailure(void)
{ RCC->CIR=(RCC->CIR & 0x00007f00U) | RCC_CIR_CSSC; }

static uint32_t ahb(unsigned div)
{
    switch (div) { case 1: return RCC_SYSCLK_DIV1; case 2: return RCC_SYSCLK_DIV2;
    case 4: return RCC_SYSCLK_DIV4; case 8: return RCC_SYSCLK_DIV8; case 16: return RCC_SYSCLK_DIV16;
    case 64: return RCC_SYSCLK_DIV64; case 128: return RCC_SYSCLK_DIV128;
    case 256: return RCC_SYSCLK_DIV256; default: return RCC_SYSCLK_DIV512; }
}
static uint32_t apb(unsigned div)
{
    switch (div) { case 1: return RCC_HCLK_DIV1; case 2: return RCC_HCLK_DIV2;
    case 4: return RCC_HCLK_DIV4; case 8: return RCC_HCLK_DIV8; default: return RCC_HCLK_DIV16; }
}
ABoxHalStatus ABoxMcuClockHw_Get(ABoxMcuClockFrequencies *out)
{
    if (!out) return ABOX_HAL_INVALID;
    out->system_hz = HAL_RCC_GetSysClockFreq(); out->ahb_hz = HAL_RCC_GetHCLKFreq();
    out->apb1_hz = HAL_RCC_GetPCLK1Freq(); out->apb2_hz = HAL_RCC_GetPCLK2Freq();
    out->apb1_timer_hz = out->apb1_hz * ((RCC->CFGR & RCC_CFGR_PPRE1) ? 2U : 1U);
    out->apb2_timer_hz = out->apb2_hz * ((RCC->CFGR & RCC_CFGR_PPRE2) ? 2U : 1U);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClockHw_Apply(const ABoxMcuClockConfig *c, const ABoxMcuClockFrequencies *expected)
{
    static const uint32_t pll[] = {RCC_PLL_MUL4,RCC_PLL_MUL5,RCC_PLL_MUL6,RCC_PLL_MUL7,RCC_PLL_MUL8,RCC_PLL_MUL9};
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clock = {0};
    ABoxMcuClockFrequencies actual;
    uint32_t latency;
    int cascade = c->source == ABOX_MCU_CLOCK_PLL_AUX_HSE || c->source == ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS;
    int bypass = c->source == ABOX_MCU_CLOCK_HSE_BYPASS || c->source == ABOX_MCU_CLOCK_PLL_HSE_BYPASS || c->source == ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS;
    int external = bypass || cascade || c->source == ABOX_MCU_CLOCK_HSE || c->source == ABOX_MCU_CLOCK_PLL_HSE;
    int use_pll = cascade || c->source == ABOX_MCU_CLOCK_PLL_HSI || c->source == ABOX_MCU_CLOCK_PLL_HSE || c->source == ABOX_MCU_CLOCK_PLL_HSE_BYPASS;
    if (external && c->external_hz != HSE_VALUE) return ABOX_HAL_INVALID;
    if (__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_HSI ||
        (RCC->CR & RCC_CR_PLLON) || __HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY)) return ABOX_HAL_BUSY;
    if (cascade && ((RCC->CR & (RCC_CR_PLL2ON|RCC_CR_PLL2RDY)) ||
        ((RCC->CR & RCC_CR_PLL3ON) && (RCC->CFGR2 & RCC_CFGR2_PREDIV2)!=((uint32_t)(c->aux_divider-1U)<<4U)))) return ABOX_HAL_BUSY;
    if (external && (RCC->CR & RCC_CR_HSEON) && !!(RCC->CR & RCC_CR_HSEBYP)!=bypass) return ABOX_HAL_BUSY;
    if (external && !(RCC->CR & RCC_CR_HSEON)) RCC->CR &= ~RCC_CR_HSEBYP;
    osc.OscillatorType = external ? RCC_OSCILLATORTYPE_HSE : RCC_OSCILLATORTYPE_HSI;
    osc.HSEState = external ? (bypass ? RCC_HSE_BYPASS : RCC_HSE_ON) : RCC_HSE_OFF;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.HSEPredivValue = external && use_pll ? (uint32_t)(c->external_divider - 1U) : RCC_HSE_PREDIV_DIV1;
    osc.Prediv1Source = cascade ? RCC_PREDIV1_SOURCE_PLL2 : RCC_PREDIV1_SOURCE_HSE;
    osc.PLL.PLLState = use_pll ? RCC_PLL_ON : RCC_PLL_NONE;
    osc.PLL.PLLSource = external ? RCC_PLLSOURCE_HSE : RCC_PLLSOURCE_HSI_DIV2;
    if (use_pll) {
        if(c->pll_multiplier!=ABOX_MCU_CLOCK_PLL_MUL_6_5 && (c->pll_multiplier<4U || c->pll_multiplier>9U)) return ABOX_HAL_INVALID;
        osc.PLL.PLLMUL = c->pll_multiplier==ABOX_MCU_CLOCK_PLL_MUL_6_5 ? RCC_PLL_MUL6_5 : pll[c->pll_multiplier - 4U];
    }
    osc.PLL2.PLL2State = cascade ? RCC_PLL2_ON : RCC_PLL_NONE;
    if (cascade) {
        osc.PLL2.HSEPrediv2Value=(uint32_t)(c->aux_divider-1U)<<4U;
        osc.PLL2.PLL2MUL=(uint32_t)(c->aux_multiplier==20U ? 15U : c->aux_multiplier-2U)<<8U;
    }
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return ABOX_HAL_ERROR;
    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = use_pll ? RCC_SYSCLKSOURCE_PLLCLK : external ? RCC_SYSCLKSOURCE_HSE : RCC_SYSCLKSOURCE_HSI;
    clock.AHBCLKDivider = ahb(c->ahb_divider); clock.APB1CLKDivider = apb(c->apb1_divider);
    clock.APB2CLKDivider = apb(c->apb2_divider);
    latency = expected->ahb_hz > 48000000U ? FLASH_LATENCY_2 : expected->ahb_hz > 24000000U ? FLASH_LATENCY_1 : FLASH_LATENCY_0;
    if (HAL_RCC_ClockConfig(&clock, latency) != HAL_OK || ABoxMcuClockHw_Get(&actual) != ABOX_HAL_OK) return ABOX_HAL_ERROR;
    return actual.system_hz == expected->system_hz && actual.ahb_hz == expected->ahb_hz &&
           actual.apb1_hz == expected->apb1_hz && actual.apb2_hz == expected->apb2_hz ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
