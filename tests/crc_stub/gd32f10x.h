#ifndef ABOX_TEST_GD32_CRC_H
#define ABOX_TEST_GD32_CRC_H
#include <stdint.h>
extern uint32_t test_crc_words[3];
#define CRC_BASE ((uintptr_t)&test_crc_words[0])
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define RCU_CRC 1U
void rcu_periph_clock_enable(uint32_t id);
#endif
