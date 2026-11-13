#include "abox_gd32_monotonic.h"
#include "gd32f10x.h"

/* TIMER1 is the peripheral at the STM32 TIM2 address. It counts at 2 kHz;
 * the reader and ISR consume a pending overflow under the same PRIMASK. */
static volatile uint64_t g_high;
static volatile uint8_t g_ready;

int ABoxGdMonotonic_Init(void)
{
    timer_parameter_struct config;
    uint32_t clock, divider;
    if (g_ready) return 1;
    clock = rcu_clock_freq_get(CK_APB1);
    if (RCU_CFG0 & (1U << 10)) clock *= 2U;
    divider = clock / 2000U;
    if (clock % 2000U || !divider || divider > 65536U) return 0;
    rcu_periph_clock_enable(RCU_TIMER1);
    timer_deinit(TIMER1);
    timer_struct_para_init(&config);
    config.prescaler = (uint16_t)(divider - 1U);
    config.alignedmode = TIMER_COUNTER_EDGE;
    config.counterdirection = TIMER_COUNTER_UP;
    config.period = 65535U;
    config.clockdivision = TIMER_CKDIV_DIV1;
    timer_init(TIMER1, &config);
    timer_flag_clear(TIMER1, TIMER_FLAG_UP);
    g_high = 0U;
    NVIC_SetPriority(TIMER1_IRQn, 0U);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);
    timer_interrupt_enable(TIMER1, TIMER_INT_UP);
    g_ready = 1U;
    timer_enable(TIMER1);
    return 1;
}

uint64_t ABoxGdMonotonic_GetMs(void)
{
    uint32_t key, before, count, after;
    uint64_t ticks;
    if (!g_ready) return 0U;
    key = __get_PRIMASK();
    __disable_irq();
    before = timer_flag_get(TIMER1, TIMER_FLAG_UP);
    count = timer_counter_read(TIMER1);
    after = timer_flag_get(TIMER1, TIMER_FLAG_UP);
    if (before != RESET || after != RESET) {
        count = timer_counter_read(TIMER1);
        timer_flag_clear(TIMER1, TIMER_FLAG_UP);
        g_high += 65536ULL;
    }
    ticks = g_high + count;
    __set_PRIMASK(key);
    return ticks / 2U;
}

void ABoxGdMonotonic_IrqHandler(void) { (void)ABoxGdMonotonic_GetMs(); }
