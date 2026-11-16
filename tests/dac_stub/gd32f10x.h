#ifndef GD32F10X_H
#define GD32F10X_H
#include <stdint.h>
#define RCU_DAC 1U
extern unsigned clocks;
void rcu_periph_clock_enable(uint32_t clock);
#endif
