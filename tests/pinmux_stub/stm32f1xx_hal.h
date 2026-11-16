#ifndef TEST_PINMUX_STM32_H
#define TEST_PINMUX_STM32_H
#include <stdint.h>
typedef struct { uint32_t EVCR; uint32_t MAPR; } AFIO_TypeDef;
extern AFIO_TypeDef test_afio;
extern unsigned test_clock_count;
#define AFIO (&test_afio)
#define __HAL_RCC_AFIO_CLK_ENABLE() (++test_clock_count)
#endif
