#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H
#include <stdint.h>
typedef struct { uint32_t IMR,EMR,RTSR,FTSR,SWIER,PR; } ExtiRegisters;
typedef struct { uint32_t EVCR,MAPR,EXTICR[4]; } AfioRegisters;
extern ExtiRegisters exti_regs;
extern AfioRegisters afio_regs;
extern unsigned afio_clocks;
#define EXTI (&exti_regs)
#define AFIO (&afio_regs)
#define __HAL_RCC_AFIO_CLK_ENABLE() (++afio_clocks)
#endif
