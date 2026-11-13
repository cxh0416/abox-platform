#ifndef GD32_BOARD_TEST_STUB_H
#define GD32_BOARD_TEST_STUB_H
#include <stdint.h>
typedef enum { RESET = 0, SET = 1 } FlagStatus;
#define GPIOA 0U
#define GPIOB 1U
#define GPIOC 2U
#define RCU_GPIOA 0U
#define RCU_GPIOB 1U
#define RCU_GPIOC 2U
#define GPIO_MODE_OUT_PP 0x10U
#define GPIO_MODE_IN_FLOATING 0x04U
#define GPIO_OSPEED_2MHZ 0x02U
void rcu_periph_clock_enable(uint32_t port);
void gpio_bit_set(uint32_t port, uint32_t pin);
void gpio_bit_reset(uint32_t port, uint32_t pin);
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pin);
FlagStatus gpio_input_bit_get(uint32_t port, uint32_t pin);
FlagStatus gpio_output_bit_get(uint32_t port, uint32_t pin);
uint32_t test_get_primask(void);
void test_disable_irq(void);
void test_set_primask(uint32_t key);
#define __get_PRIMASK() test_get_primask()
#define __disable_irq() test_disable_irq()
#define __set_PRIMASK(k) test_set_primask(k)
#endif
