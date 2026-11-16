#ifndef ABOX_TEST_SYSTEM_STM32_H
#define ABOX_TEST_SYSTEM_STM32_H
#include <stdint.h>
typedef struct { uint32_t CR,CFGR,CFGR2; } TestRcc;
extern TestRcc test_rcc;
#define RCC (&test_rcc)
#define STM32F105xC 1
#define HSI_VALUE 8000000U
#ifndef HSE_VALUE
#define HSE_VALUE 8000000U
#endif
#define RCC_CFGR_SWS (3U<<2)
#define RCC_CFGR_PLLMULL (15U<<18)
#define RCC_CFGR_PLLSRC (1U<<16)
#define RCC_CFGR_HPRE (15U<<4)
#define RCC_CFGR2_PREDIV1SRC (1U<<16)
#define RCC_CFGR2_PREDIV1 15U
#define RCC_CFGR2_PREDIV2 (15U<<4)
#define RCC_CFGR2_PLL2MUL (15U<<8)
#endif
