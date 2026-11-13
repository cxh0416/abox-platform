#ifndef TEST_GD_WATCHDOG_H
#define TEST_GD_WATCHDOG_H
#include <stdint.h>
extern volatile uint32_t test_watchdog_registers[4];
#define FWDGT_BASE 0U
#define REG32(address) test_watchdog_registers[(address) / 4U]
#define DBG_FWDGT_HOLD (1U << 8U)
void dbg_periph_enable(uint32_t flags);
#endif
