#ifndef TEST_GPIO_STM32_H
#define TEST_GPIO_STM32_H
#include <stdint.h>
typedef struct { uint32_t ODR, LCKR; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
extern GPIO_TypeDef test_gpio[5];
#define GPIOA (&test_gpio[0])
#define GPIOB (&test_gpio[1])
#define GPIOC (&test_gpio[2])
#define GPIOD (&test_gpio[3])
#define GPIOE (&test_gpio[4])
#define GPIO_MODE_INPUT 1U
#define GPIO_MODE_ANALOG 2U
#define GPIO_MODE_OUTPUT_PP 3U
#define GPIO_MODE_OUTPUT_OD 4U
#define GPIO_MODE_AF_PP 5U
#define GPIO_MODE_AF_OD 6U
#define GPIO_NOPULL 0U
#define GPIO_PULLUP 1U
#define GPIO_PULLDOWN 2U
#define GPIO_SPEED_FREQ_LOW 2U
#define GPIO_SPEED_FREQ_MEDIUM 10U
#define GPIO_SPEED_FREQ_HIGH 50U
#define GPIO_LCKR_LCKK (1U << 16)
#define HAL_OK 0U
void test_gpio_clock(unsigned port);
#define __HAL_RCC_GPIOA_CLK_ENABLE() test_gpio_clock(0U)
#define __HAL_RCC_GPIOB_CLK_ENABLE() test_gpio_clock(1U)
#define __HAL_RCC_GPIOC_CLK_ENABLE() test_gpio_clock(2U)
#define __HAL_RCC_GPIOD_CLK_ENABLE() test_gpio_clock(3U)
#define __HAL_RCC_GPIOE_CLK_ENABLE() test_gpio_clock(4U)
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
uint32_t HAL_GPIO_LockPin(GPIO_TypeDef *port, uint16_t pins);
#endif
