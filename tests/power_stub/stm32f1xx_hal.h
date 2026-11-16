#ifndef ABOX_TEST_STM32_POWER_H
#define ABOX_TEST_STM32_POWER_H
#include <stdint.h>
typedef struct { uint32_t reserved;uint32_t DR1;uint32_t x[8];uint32_t DR10;uint32_t middle[5];uint32_t DR11;uint32_t y[30];uint32_t DR42; } BKP_TypeDef;
typedef struct { uint32_t CR,CSR; } PWR_TypeDef;
typedef struct { uint32_t IMR,EMR,RTSR,FTSR,SWIER,PR; } EXTI_TypeDef;
extern BKP_TypeDef test_bkp;extern PWR_TypeDef test_pwr;extern EXTI_TypeDef test_exti;
extern unsigned pwr_clocks,bkp_clocks;
#define BKP_BASE ((uintptr_t)&test_bkp)
#define PWR (&test_pwr)
#define EXTI (&test_exti)
#define PWR_CR_DBP (1U<<8)
#define PWR_CR_PVDE (1U<<4)
#define PWR_CR_PLS (7U<<5)
#define PWR_CSR_PVDO (1U<<2)
#define __HAL_RCC_PWR_CLK_ENABLE() (++pwr_clocks)
#define __HAL_RCC_BKP_CLK_ENABLE() (++bkp_clocks)
#endif
