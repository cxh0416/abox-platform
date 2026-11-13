#include <assert.h>
#include "abox_watchdog_port.h"
#include "gd32f10x.h"

volatile uint32_t test_watchdog_registers[4];
static uint32_t debug_flags;
void dbg_periph_enable(uint32_t flags) { debug_flags = flags; }

int main(void)
{
    assert(!ABoxWatchdog_Refresh());
    assert(!ABoxWatchdog_Init(12U, 1250U));
    assert(!ABoxWatchdog_Init(64U, 4096U));
    assert(test_watchdog_registers[0] == 0U);
    test_watchdog_registers[3] = 1U;
    assert(!ABoxWatchdog_Init(64U, 1250U));
    assert(!ABoxWatchdog_Refresh());
    test_watchdog_registers[3] = 0U;
    assert(ABoxWatchdog_Init(64U, 1250U));
    assert(test_watchdog_registers[1] == 4U && test_watchdog_registers[2] == 1250U);
    assert(test_watchdog_registers[0] == 0xAAAAU);
    test_watchdog_registers[0] = 0U;
    assert(ABoxWatchdog_Refresh() && test_watchdog_registers[0] == 0xAAAAU);
    assert(!ABoxWatchdog_Init(64U, 1250U));
    ABoxWatchdog_FreezeWhileDebugging();
    assert(debug_flags == DBG_FWDGT_HOLD);
    return 0;
}
