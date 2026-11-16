#include <assert.h>
#include <stdint.h>
#include "abox_pinmux.h"
#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
AFIO_TypeDef test_afio;
#define MAPR test_afio.MAPR
#else
#include "gd32f10x.h"
uint32_t test_afio_pcf0;
#define MAPR test_afio_pcf0
void rcu_periph_clock_enable(uint32_t peripheral)
{
    extern unsigned test_clock_count;
    assert(peripheral == RCU_AF); ++test_clock_count;
}
#endif
unsigned test_clock_count;
static unsigned locks, unlocks;
uint32_t ABoxCritical_Enter(void) { ++locks; return 7U; }
void ABoxCritical_Exit(uint32_t state) { assert(state == 7U); ++unlocks; }

int main(void)
{
    static const struct { ABoxPinMuxSignal signal; unsigned shift, width, routes; } fields[] = {
        {ABOX_PINMUX_SPI1,0,1,3}, {ABOX_PINMUX_I2C1,1,1,3},
        {ABOX_PINMUX_UART1,2,1,3}, {ABOX_PINMUX_UART2,3,1,3},
        {ABOX_PINMUX_UART3,4,2,11}, {ABOX_PINMUX_TIMER1,6,2,11},
        {ABOX_PINMUX_TIMER2,8,2,15}, {ABOX_PINMUX_TIMER3,10,2,13},
        {ABOX_PINMUX_TIMER4,12,1,3}, {ABOX_PINMUX_CAN1,13,2,13},
        {ABOX_PINMUX_PD01,15,1,3}, {ABOX_PINMUX_TIMER5_CH4,16,1,3},
        {ABOX_PINMUX_CAN2,22,1,3}, {ABOX_PINMUX_SPI3,28,1,3},
        {ABOX_PINMUX_TIMER2_ITR1,29,1,3}
    };
    for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        for (unsigned route = 0; route < 4U; ++route) {
            uint32_t before = 0xE7A5A55AU, mask = ((1U << fields[i].width)-1U) << fields[i].shift;
            MAPR = before;
            int ok = ABoxPinMux_Configure(fields[i].signal, (ABoxPinMuxRoute)route, ABOX_PINMUX_DEBUG_SWD_ONLY);
            if (fields[i].routes & (1U << route)) {
                assert(ok && MAPR == ((before & ~(mask | (7U << 24U))) | (route << fields[i].shift) | (2U << 24U)));
            } else assert(!ok && MAPR == before);
        }
    }
    MAPR = 0xFFFFFFFFU;
    assert(ABoxPinMux_SetDebug(ABOX_PINMUX_DEBUG_FULL) && (MAPR & (7U << 24U)) == 0U);
    assert(ABoxPinMux_SetDebug(ABOX_PINMUX_DEBUG_NO_NJTRST) && (MAPR & (7U << 24U)) == (1U << 24U));
    assert(ABoxPinMux_SetDebug(ABOX_PINMUX_DEBUG_DISABLED) && (MAPR & (7U << 24U)) == (4U << 24U));
    unsigned old = test_clock_count;
    assert(!ABoxPinMux_SetDebug((ABoxPinMuxDebug)99));
    assert(!ABoxPinMux_Configure((ABoxPinMuxSignal)99, ABOX_PINMUX_ROUTE_0, ABOX_PINMUX_DEBUG_FULL));
    assert(!ABoxPinMux_Configure(ABOX_PINMUX_SPI1, (ABoxPinMuxRoute)-1, ABOX_PINMUX_DEBUG_FULL));
    assert(test_clock_count == old && locks == old && unlocks == old);
    return 0;
}
