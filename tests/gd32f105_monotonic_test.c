#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "gd32f10x.h"
#include "../hardware/mcu/gd32f105/monotonic_gd32f105.c"

uint32_t test_rcu_cfg0 = 1U << 10;
static uint32_t counter, flag, primask, clock = 36000000U;
uint32_t rcu_clock_freq_get(uint32_t c) { assert(c == CK_APB1); return clock; }
void rcu_periph_clock_enable(uint32_t p) { assert(p == RCU_TIMER1); }
void timer_deinit(uint32_t t) { assert(t == TIMER1); }
void timer_struct_para_init(timer_parameter_struct *c) { memset(c, 0, sizeof(*c)); }
void timer_init(uint32_t t, timer_parameter_struct *c)
{ assert(t == TIMER1 && c->prescaler == 35999U && c->period == 65535U); flag = 1U; }
void timer_flag_clear(uint32_t t, uint32_t f) { assert(t == TIMER1 && f == TIMER_FLAG_UP); flag = 0U; }
FlagStatus timer_flag_get(uint32_t t, uint32_t f) { assert(t == TIMER1 && f == TIMER_FLAG_UP); return flag ? SET : RESET; }
uint32_t timer_counter_read(uint32_t t) { assert(t == TIMER1); return counter; }
void timer_interrupt_enable(uint32_t t, uint32_t i) { assert(t == TIMER1 && i == TIMER_INT_UP); }
void timer_enable(uint32_t t) { assert(t == TIMER1); }
void NVIC_SetPriority(int i, uint32_t p) { assert(i == TIMER1_IRQn && !p); }
void NVIC_ClearPendingIRQ(int i) { assert(i == TIMER1_IRQn); }
void NVIC_EnableIRQ(int i) { assert(i == TIMER1_IRQn); }
uint32_t test_get_primask(void) { return primask; }
void test_disable_irq(void) { primask = 1U; }
void test_set_primask(uint32_t p) { primask = p; }
static void advance(uint32_t n)
{ uint32_t next = counter + n; assert(n < 65536U); if (next >= 65536U) flag = 1U; counter = next & 65535U; }
int main(void)
{
    assert(ABoxGdMonotonic_GetMs() == 0U);
    clock = 36000001U; assert(!ABoxGdMonotonic_Init());
    clock = 36000000U; assert(ABoxGdMonotonic_Init());
    advance(65530U); uint64_t before = ABoxGdMonotonic_GetMs();
    primask = 1U; advance(80U);
    assert(ABoxGdMonotonic_GetMs() == before + 40U && primask == 1U);
    before = ABoxGdMonotonic_GetMs(); ABoxGdMonotonic_IrqHandler();
    assert(ABoxGdMonotonic_GetMs() == before);
    advance(64000U); assert(ABoxGdMonotonic_GetMs() == before + 32000U);
    for (uint32_t phase = 0U; phase < 65536U; ++phase) {
        const uint32_t gaps[] = {80U, 64000U, 65535U};
        for (unsigned i = 0U; i < 3U; ++i) {
            g_high = 100ULL * 65536ULL; counter = phase; flag = 0U;
            advance(gaps[i]);
            assert(ABoxGdMonotonic_GetMs() == (100ULL * 65536ULL + phase + gaps[i]) / 2U);
        }
    }
    return 0;
}
