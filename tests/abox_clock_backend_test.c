#include <assert.h>
#include <stdint.h>
#include "abox_clock_backend.h"
static unsigned clock_source,pll_ready,fail_osc,fail_config,fail_latency,osc_calls,clock_calls;
static uint32_t system_hz=72000000U,ahb_hz=72000000U,apb1_hz=36000000U,apb2_hz=72000000U;
static unsigned pll_mul,prediv,aux_mul,aux_div,aux_source,apb1_div,latency_seen,bypass_seen;
#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
RCC_TypeDef fake_rcc={.CFGR=RCC_CFGR_PPRE1};
#define TEST_CLOCK_CONTROL fake_rcc.CR
#define TEST_CLOCK_CONFIG fake_rcc.CFGR
#define TEST_CLOCK_INTERRUPT fake_rcc.CIR
unsigned fake_sys_source,fake_pll_ready;
uint32_t HAL_RCC_GetSysClockFreq(void) { return system_hz; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return ahb_hz; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return apb1_hz; }
uint32_t HAL_RCC_GetPCLK2Freq(void) { return apb2_hz; }
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *c)
{ ++osc_calls;pll_mul=c->PLL.PLLMUL;prediv=c->HSEPredivValue;bypass_seen=c->HSEState==RCC_HSE_BYPASS;
  aux_mul=c->PLL2.PLL2MUL;aux_div=c->PLL2.HSEPrediv2Value;aux_source=c->Prediv1Source;return fail_osc?HAL_ERROR:HAL_OK; }
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *c,uint32_t latency)
{ ++clock_calls;apb1_div=c->APB1CLKDivider;latency_seen=latency;return fail_config?HAL_ERROR:HAL_OK; }
static void set_busy(void) { fake_rcc.CR=RCC_CR_PLLON; }
static void clear_busy(void) { fake_rcc.CR=0; }
#else
#include "gd32f10x.h"
uint32_t fake_rcu_ctl,fake_rcu_cfg=RCU_CFG0_APB1PSC,fake_rcu_cfg1,fake_rcu_int,fake_fmc_ws,SystemCoreClock;
#define TEST_CLOCK_CONTROL fake_rcu_ctl
#define TEST_CLOCK_CONFIG fake_rcu_cfg
#define TEST_CLOCK_INTERRUPT fake_rcu_int
uint32_t rcu_clock_freq_get(uint32_t c) { return c==CK_SYS?system_hz:c==CK_AHB?ahb_hz:c==CK_APB1?apb1_hz:apb2_hz; }
uint32_t rcu_system_clock_source_get(void) { return clock_source; }
FlagStatus rcu_flag_get(uint32_t f) { (void)f;return pll_ready?SET:RESET; }
void fmc_wscnt_set(uint32_t l) { if(!fail_latency)fake_fmc_ws=l;latency_seen=l; }
void rcu_osci_on(uint32_t source) { if(source==RCU_HXTAL)bypass_seen=!!(RCU_CTL & RCU_CTL_HXTALBPS);++osc_calls; }
ErrStatus rcu_osci_stab_wait(uint32_t source) { (void)source;return fail_osc?ERROR:SUCCESS; }
void rcu_ahb_clock_config(uint32_t d) { (void)d; }
void rcu_apb1_clock_config(uint32_t d) { apb1_div=d; }
void rcu_apb2_clock_config(uint32_t d) { (void)d; }
void rcu_predv0_config(uint32_t source,uint32_t d) { aux_source=source;prediv=d; }
void rcu_predv1_config(uint32_t d) { aux_div=d; }
void rcu_pll1_config(uint32_t mul) { aux_mul=mul; }
void rcu_pll_config(uint32_t source,uint32_t mul) { (void)source;pll_mul=mul; }
void rcu_system_clock_source_config(uint32_t source) { if(!fail_config)clock_source=source;++clock_calls; }
static void set_busy(void) { fake_rcu_ctl=RCU_CTL_PLLEN; }
static void clear_busy(void) { fake_rcu_ctl=0; }
#endif
int main(void)
{
    ABoxMcuClockConfig c={ABOX_MCU_CLOCK_PLL_HSE,8000000U,1U,9U,1U,2U,1U,0U,0U};
    ABoxMcuClockFrequencies f={72000000U,72000000U,36000000U,72000000U,72000000U,72000000U},got;
    ABoxMcuClockOutput output;uint8_t pending;
    assert(ABoxMcuClock_SetOutput((ABoxMcuClockOutput)10)==ABOX_HAL_INVALID);
    assert(ABoxMcuClock_GetOutput(0)==ABOX_HAL_INVALID);
    assert(ABoxMcuClock_SetOutput(ABOX_MCU_CLOCK_OUT_I2S_PLL)==ABOX_HAL_OK);
    assert((TEST_CLOCK_CONFIG & (15U<<24))==(11U<<24) && (TEST_CLOCK_CONFIG & 0x100U));
    assert(ABoxMcuClock_GetOutput(&output)==ABOX_HAL_OK && output==ABOX_MCU_CLOCK_OUT_I2S_PLL);
    assert(ABoxMcuClock_SetOutput(ABOX_MCU_CLOCK_OUT_OFF)==ABOX_HAL_OK);
    assert(ABoxMcuClock_SetFailureMonitor(2U)==ABOX_HAL_INVALID);
    assert(ABoxMcuClock_SetFailureMonitor(1U)==ABOX_HAL_OK && (TEST_CLOCK_CONTROL & (1U<<19)));
    assert(ABoxMcuClock_FailurePending(0)==ABOX_HAL_INVALID);
    TEST_CLOCK_INTERRUPT=0x7fffU;
    assert(ABoxMcuClock_FailurePending(&pending)==ABOX_HAL_OK && pending==1U);
    ABoxMcuClock_ClearFailure();assert(TEST_CLOCK_INTERRUPT==((1U<<23)|0x7f00U));
    assert(ABoxMcuClock_SetFailureMonitor(0U)==ABOX_HAL_OK && !(TEST_CLOCK_CONTROL & (1U<<19)));
    assert(ABoxMcuClockHw_Get(&got)==ABOX_HAL_OK && got.apb1_timer_hz==72000000U);
    c.external_hz=12000000U;assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_INVALID && !osc_calls);c.external_hz=8000000U;
    set_busy();assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_BUSY && !osc_calls);clear_busy();
    fail_osc=1U;assert(ABoxMcuClockHw_Apply(&c,&f)!=ABOX_HAL_OK);fail_osc=0U;
    osc_calls=0;clock_calls=0;
    assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_OK);
    assert(osc_calls && clock_calls && pll_mul==9U && prediv==0U && apb1_div==2U && latency_seen==2U);
    c.source=ABOX_MCU_CLOCK_PLL_HSE_BYPASS;
#ifdef TEST_STM32
    fake_sys_source=0U;fake_rcc.CR=RCC_CR_HSEON;
#else
    clock_source=RCU_SCSS_IRC8M;fake_rcu_ctl=RCU_CTL_HXTALEN;
#endif
    assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_BUSY);
    clear_busy();assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_OK && bypass_seen && pll_mul==9U && prediv==0U);
    c.source=ABOX_MCU_CLOCK_PLL_AUX_HSE;c.aux_divider=2U;c.aux_multiplier=16U;c.external_divider=8U;
#ifdef TEST_STM32
    fake_sys_source=0U;fake_rcc.CR=RCC_CR_PLL2ON;
#else
    clock_source=RCU_SCSS_IRC8M;fake_rcu_ctl=RCU_CTL_PLL1EN;
#endif
    assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_BUSY);
    clear_busy();assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_OK && pll_mul==9U && prediv==7U &&
                        aux_div==(1U<<4) && aux_mul==(14U<<8) && aux_source==(1U<<16));
    c.source=ABOX_MCU_CLOCK_PLL_HSI;c.pll_multiplier=ABOX_MCU_CLOCK_PLL_MUL_6_5;
    system_hz=ahb_hz=apb2_hz=26000000U;apb1_hz=13000000U;
    f.system_hz=f.ahb_hz=f.apb2_hz=26000000U;f.apb1_hz=13000000U;
    clear_busy();
#ifdef TEST_STM32
    fake_sys_source=0U;
#else
    clock_source=RCU_SCSS_IRC8M;
#endif
    assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_OK && pll_mul==65U);
#ifndef TEST_STM32
    c.pll_multiplier=27U;system_hz=ahb_hz=apb2_hz=108000000U;apb1_hz=54000000U;
    f.system_hz=f.ahb_hz=f.apb2_hz=108000000U;f.apb1_hz=54000000U;clock_source=RCU_SCSS_IRC8M;
    assert(ABoxMcuClockHw_Apply(&c,&f)==ABOX_HAL_OK && pll_mul==27U);
#endif
    return 0;
}
