#ifndef TEST_GPIO_GD32_H
#define TEST_GPIO_GD32_H
#include <stdint.h>
#define GPIOA 0U
#define GPIOB 1U
#define GPIOC 2U
#define GPIOD 3U
#define GPIOE 4U
#define RCU_GPIOA 10U
#define RCU_GPIOB 11U
#define RCU_GPIOC 12U
#define RCU_GPIOD 13U
#define RCU_GPIOE 14U
#define GPIO_MODE_AIN 0U
#define GPIO_MODE_IN_FLOATING 1U
#define GPIO_MODE_IPU 2U
#define GPIO_MODE_IPD 3U
#define GPIO_MODE_OUT_PP 4U
#define GPIO_MODE_OUT_OD 5U
#define GPIO_MODE_AF_PP 6U
#define GPIO_MODE_AF_OD 7U
#define GPIO_OSPEED_2MHZ 2U
#define GPIO_OSPEED_10MHZ 10U
#define GPIO_OSPEED_50MHZ 50U
#define RESET 0U
extern uint32_t test_gpio_lock[5];
#define GPIO_LOCK(port) test_gpio_lock[port]
#define GPIO_LOCK_LKK (1U << 16)
void rcu_periph_clock_enable(uint32_t clock);
void gpio_bit_set(uint32_t port, uint32_t mask);
void gpio_bit_reset(uint32_t port, uint32_t mask);
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t mask);
uint32_t gpio_input_bit_get(uint32_t port, uint32_t mask);
uint32_t gpio_output_bit_get(uint32_t port, uint32_t mask);
void gpio_pin_lock(uint32_t port, uint32_t mask);
#endif
