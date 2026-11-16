#ifndef GD32F10X_H
#define GD32F10X_H
#include <stdint.h>
#define RCU_AF 1U
extern unsigned afio_clocks, selected_port, selected_pin;
void rcu_periph_clock_enable(uint32_t clock);
void gpio_exti_source_select(uint8_t port, uint8_t pin);
#endif
