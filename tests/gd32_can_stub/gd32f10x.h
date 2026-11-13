#ifndef TEST_GD32F10X_H
#define TEST_GD32F10X_H
#include <stdint.h>
#define CAN0 10U
#define CAN1 11U
#define GPIOA 20U
#define GPIOB 21U
#define RCU_CAN0 30U
#define RCU_CAN1 31U
#define RCU_GPIOA 32U
#define RCU_GPIOB 33U
#define RCU_AF 34U
#define CK_APB1 35U
#define GPIO_MODE_IN_FLOATING 40U
#define GPIO_MODE_AF_PP 41U
#define GPIO_MODE_OUT_PP 46U
#define GPIO_OSPEED_2MHZ 42U
#define GPIO_OSPEED_50MHZ 43U
#define GPIO_CAN1_REMAP 44U
#define GPIO_CAN0_FULL_REMAP 45U
#define DISABLE 0U
#define ENABLE 1U
#define SUCCESS 1U
uint32_t rcu_clock_freq_get(uint32_t clock);
void rcu_periph_clock_enable(uint32_t peripheral);
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pins);
void gpio_bit_reset(uint32_t port, uint32_t pins);
void gpio_pin_remap_config(uint32_t remap, uint32_t enabled);
#endif
