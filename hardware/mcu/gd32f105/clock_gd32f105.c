#include "abox_clock_backend.h"
#include "gd32f10x.h"

static int output_valid(unsigned source)
{ return source==0U || (source>=4U && source<=9U) || source==11U; }
ABoxHalStatus ABoxMcuClock_SetOutput(ABoxMcuClockOutput source)
{
    if (!output_valid((unsigned)source)) return ABOX_HAL_INVALID;
    RCU_CFG0=(RCU_CFG0 & ~RCU_CFG0_CKOUT0SEL) | ((uint32_t)source<<24U);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClock_GetOutput(ABoxMcuClockOutput *source)
{
    unsigned value;if (!source) return ABOX_HAL_INVALID;
    value=(RCU_CFG0 & RCU_CFG0_CKOUT0SEL)>>24U;if (!output_valid(value)) return ABOX_HAL_ERROR;
    *source=(ABoxMcuClockOutput)value;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClock_SetFailureMonitor(uint8_t enabled)
{
    if (enabled>1U) return ABOX_HAL_INVALID;
    if (enabled) RCU_CTL |= RCU_CTL_CKMEN;else RCU_CTL &= ~RCU_CTL_CKMEN;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClock_FailurePending(uint8_t *pending)
{ if (!pending) return ABOX_HAL_INVALID;*pending=(RCU_INT & RCU_INT_CKMIF) ? 1U : 0U;return ABOX_HAL_OK; }
void ABoxMcuClock_ClearFailure(void)
{ RCU_INT=(RCU_INT & 0x00007f00U) | RCU_INT_CKMIC; }

static uint32_t ahb(unsigned div)
{
    switch (div) { case 1: return RCU_AHB_CKSYS_DIV1; case 2: return RCU_AHB_CKSYS_DIV2;
    case 4: return RCU_AHB_CKSYS_DIV4; case 8: return RCU_AHB_CKSYS_DIV8; case 16: return RCU_AHB_CKSYS_DIV16;
    case 64: return RCU_AHB_CKSYS_DIV64; case 128: return RCU_AHB_CKSYS_DIV128;
    case 256: return RCU_AHB_CKSYS_DIV256; default: return RCU_AHB_CKSYS_DIV512; }
}
static uint32_t apb1(unsigned div)
{
    switch (div) { case 1: return RCU_APB1_CKAHB_DIV1; case 2: return RCU_APB1_CKAHB_DIV2;
    case 4: return RCU_APB1_CKAHB_DIV4; case 8: return RCU_APB1_CKAHB_DIV8; default: return RCU_APB1_CKAHB_DIV16; }
}
static uint32_t apb2(unsigned div)
{
    switch (div) { case 1: return RCU_APB2_CKAHB_DIV1; case 2: return RCU_APB2_CKAHB_DIV2;
    case 4: return RCU_APB2_CKAHB_DIV4; case 8: return RCU_APB2_CKAHB_DIV8; default: return RCU_APB2_CKAHB_DIV16; }
}
ABoxHalStatus ABoxMcuClockHw_Get(ABoxMcuClockFrequencies *out)
{
    if (!out) return ABOX_HAL_INVALID;
    out->system_hz = rcu_clock_freq_get(CK_SYS); out->ahb_hz = rcu_clock_freq_get(CK_AHB);
    out->apb1_hz = rcu_clock_freq_get(CK_APB1); out->apb2_hz = rcu_clock_freq_get(CK_APB2);
    out->apb1_timer_hz = out->apb1_hz * ((RCU_CFG0 & RCU_CFG0_APB1PSC) ? 2U : 1U);
    out->apb2_timer_hz = out->apb2_hz * ((RCU_CFG0 & RCU_CFG0_APB2PSC) ? 2U : 1U);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxMcuClockHw_Apply(const ABoxMcuClockConfig *c, const ABoxMcuClockFrequencies *expected)
{
    static const uint32_t pll[] = {RCU_PLL_MUL2,RCU_PLL_MUL3,RCU_PLL_MUL4,RCU_PLL_MUL5,RCU_PLL_MUL6,
        RCU_PLL_MUL7,RCU_PLL_MUL8,RCU_PLL_MUL9,RCU_PLL_MUL10,RCU_PLL_MUL11,RCU_PLL_MUL12,
        RCU_PLL_MUL13,RCU_PLL_MUL14,0U,RCU_PLL_MUL16,RCU_PLL_MUL17,RCU_PLL_MUL18,RCU_PLL_MUL19,
        RCU_PLL_MUL20,RCU_PLL_MUL21,RCU_PLL_MUL22,RCU_PLL_MUL23,RCU_PLL_MUL24,RCU_PLL_MUL25,
        RCU_PLL_MUL26,RCU_PLL_MUL27,RCU_PLL_MUL28,RCU_PLL_MUL29,RCU_PLL_MUL30,RCU_PLL_MUL31,RCU_PLL_MUL32};
    ABoxMcuClockFrequencies actual;
    uint32_t source, status, limit;
    int cascade = c->source == ABOX_MCU_CLOCK_PLL_AUX_HSE || c->source == ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS;
    int bypass = c->source == ABOX_MCU_CLOCK_HSE_BYPASS || c->source == ABOX_MCU_CLOCK_PLL_HSE_BYPASS || c->source == ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS;
    int external = bypass || cascade || c->source == ABOX_MCU_CLOCK_HSE || c->source == ABOX_MCU_CLOCK_PLL_HSE;
    int use_pll = cascade || c->source == ABOX_MCU_CLOCK_PLL_HSI || c->source == ABOX_MCU_CLOCK_PLL_HSE || c->source == ABOX_MCU_CLOCK_PLL_HSE_BYPASS;
    if (external && c->external_hz != HXTAL_VALUE) return ABOX_HAL_INVALID;
    if (rcu_system_clock_source_get() != RCU_SCSS_IRC8M ||
        (RCU_CTL & RCU_CTL_PLLEN) || rcu_flag_get(RCU_FLAG_PLLSTB) != RESET) return ABOX_HAL_BUSY;
    if (cascade && ((RCU_CTL & (RCU_CTL_PLL1EN|RCU_CTL_PLL1STB)) ||
        ((RCU_CTL & RCU_CTL_PLL2EN) && (RCU_CFG1 & RCU_CFG1_PREDV1)!=((uint32_t)(c->aux_divider-1U)<<4U)))) return ABOX_HAL_BUSY;
    if (external && (RCU_CTL & RCU_CTL_HXTALEN) && !!(RCU_CTL & RCU_CTL_HXTALBPS)!=bypass) return ABOX_HAL_BUSY;
    /* Conservative across the supported HCLK range. */
    fmc_wscnt_set(WS_WSCNT_2);
    if ((FMC_WS & FMC_WS_WSCNT) != WS_WSCNT_2) return ABOX_HAL_ERROR;
    if (external) {
        if (!(RCU_CTL & RCU_CTL_HXTALEN)) {
            if (bypass) RCU_CTL |= RCU_CTL_HXTALBPS;
            else RCU_CTL &= ~RCU_CTL_HXTALBPS;
        }
        rcu_osci_on(RCU_HXTAL);
        if (rcu_osci_stab_wait(RCU_HXTAL) != SUCCESS) return ABOX_HAL_TIMEOUT;
    }
    rcu_ahb_clock_config(ahb(c->ahb_divider));
    rcu_apb1_clock_config(apb1(c->apb1_divider));
    rcu_apb2_clock_config(apb2(c->apb2_divider));
    if (cascade) {
        rcu_predv1_config((uint32_t)(c->aux_divider-1U)<<4U);
        rcu_pll1_config((uint32_t)(c->aux_multiplier==20U ? 15U : c->aux_multiplier-2U)<<8U);
        rcu_osci_on(RCU_PLL1_CK);
        if (rcu_osci_stab_wait(RCU_PLL1_CK) != SUCCESS) return ABOX_HAL_TIMEOUT;
    }
    if (use_pll) {
        if (external)
            rcu_predv0_config(cascade ? RCU_PREDV0SRC_CKPLL1 : RCU_PREDV0SRC_HXTAL, (uint32_t)(c->external_divider - 1U));
        if(c->pll_multiplier!=ABOX_MCU_CLOCK_PLL_MUL_6_5 &&
           (c->pll_multiplier<2U || c->pll_multiplier>32U || c->pll_multiplier==15U))return ABOX_HAL_INVALID;
        rcu_pll_config(external ? RCU_PLLSRC_HXTAL : RCU_PLLSRC_IRC8M_DIV2,
                       c->pll_multiplier==ABOX_MCU_CLOCK_PLL_MUL_6_5 ? RCU_PLL_MUL6_5 : pll[c->pll_multiplier - 2U]);
        rcu_osci_on(RCU_PLL_CK);
        if (rcu_osci_stab_wait(RCU_PLL_CK) != SUCCESS) return ABOX_HAL_TIMEOUT;
    }
    source = use_pll ? RCU_CKSYSSRC_PLL : external ? RCU_CKSYSSRC_HXTAL : RCU_CKSYSSRC_IRC8M;
    status = use_pll ? RCU_SCSS_PLL : external ? RCU_SCSS_HXTAL : RCU_SCSS_IRC8M;
    rcu_system_clock_source_config(source);
    for (limit = 0U; limit < 100000U; ++limit) if (rcu_system_clock_source_get() == status) break;
    if (limit == 100000U) return ABOX_HAL_TIMEOUT;
    if (ABoxMcuClockHw_Get(&actual) != ABOX_HAL_OK) return ABOX_HAL_ERROR;
    SystemCoreClock = actual.ahb_hz;
    return actual.system_hz == expected->system_hz && actual.ahb_hz == expected->ahb_hz &&
           actual.apb1_hz == expected->apb1_hz && actual.apb2_hz == expected->apb2_hz ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
