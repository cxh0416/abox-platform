#ifndef ABOX_TEST_GD32_POWER_H
#define ABOX_TEST_GD32_POWER_H
#include <stdint.h>
extern uint32_t test_bkp_words[48],test_pmu_words[2],test_exti_words[6];
extern unsigned pwr_clocks,bkp_clocks;
#define BKP_BASE ((uintptr_t)&test_bkp_words[0])
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define RCU_PMU 1U
#define RCU_BKPI 2U
void rcu_periph_clock_enable(uint32_t id);
#endif
