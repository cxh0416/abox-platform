#ifndef TEST_PINMUX_GD32_H
#define TEST_PINMUX_GD32_H
#include <stdint.h>
extern uint32_t test_afio_pcf0;
#define AFIO_PCF0 test_afio_pcf0
#define RCU_AF 123U
void rcu_periph_clock_enable(uint32_t peripheral);
#endif
