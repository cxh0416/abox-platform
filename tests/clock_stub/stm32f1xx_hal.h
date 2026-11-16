#ifndef TEST_CLOCK_STM_HAL_H
#define TEST_CLOCK_STM_HAL_H
#include <stdint.h>
typedef enum { HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t CR,CFGR,CIR,CFGR2; } RCC_TypeDef;
extern RCC_TypeDef fake_rcc;
#define RCC (&fake_rcc)
#define HSE_VALUE 8000000U
#define RCC_CFGR_PPRE1 0x100U
#define RCC_CFGR_PPRE2 0x200U
#define RCC_CFGR_MCO (15U<<24)
#define RCC_CR_PLLON 0x1000U
#define RCC_CR_HSEON 0x10000U
#define RCC_CR_HSEBYP 0x40000U
#define RCC_CR_CSSON (1U<<19)
#define RCC_CR_PLL2ON (1U<<26)
#define RCC_CR_PLL2RDY (1U<<27)
#define RCC_CR_PLL3ON (1U<<28)
#define RCC_CFGR2_PREDIV2 (15U<<4)
#define RCC_CIR_CSSF (1U<<7)
#define RCC_CIR_CSSC (1U<<23)
#define RCC_FLAG_PLLRDY 1U
#define RCC_SYSCLKSOURCE_STATUS_HSI 0U
#define RCC_SYSCLKSOURCE_HSI 0U
#define RCC_SYSCLKSOURCE_HSE 1U
#define RCC_SYSCLKSOURCE_PLLCLK 2U
#define RCC_PLL_MUL4 4U
#define RCC_PLL_MUL5 5U
#define RCC_PLL_MUL6 6U
#define RCC_PLL_MUL7 7U
#define RCC_PLL_MUL8 8U
#define RCC_PLL_MUL9 9U
#define RCC_PLL_MUL6_5 65U
#define RCC_SYSCLK_DIV1 1U
#define RCC_SYSCLK_DIV2 2U
#define RCC_SYSCLK_DIV4 4U
#define RCC_SYSCLK_DIV8 8U
#define RCC_SYSCLK_DIV16 16U
#define RCC_SYSCLK_DIV64 64U
#define RCC_SYSCLK_DIV128 128U
#define RCC_SYSCLK_DIV256 256U
#define RCC_SYSCLK_DIV512 512U
#define RCC_HCLK_DIV1 1U
#define RCC_HCLK_DIV2 2U
#define RCC_HCLK_DIV4 4U
#define RCC_HCLK_DIV8 8U
#define RCC_HCLK_DIV16 16U
#define RCC_OSCILLATORTYPE_HSE 1U
#define RCC_OSCILLATORTYPE_HSI 2U
#define RCC_HSE_ON 1U
#define RCC_HSE_BYPASS 2U
#define RCC_HSE_OFF 0U
#define RCC_HSI_ON 1U
#define RCC_HSICALIBRATION_DEFAULT 16U
#define RCC_HSE_PREDIV_DIV1 0U
#define RCC_PREDIV1_SOURCE_HSE 0U
#define RCC_PREDIV1_SOURCE_PLL2 (1U<<16)
#define RCC_PLL_ON 1U
#define RCC_PLL_NONE 0U
#define RCC_PLL2_ON 1U
#define RCC_PLLSOURCE_HSE 1U
#define RCC_PLLSOURCE_HSI_DIV2 0U
#define RCC_CLOCKTYPE_HCLK 1U
#define RCC_CLOCKTYPE_SYSCLK 2U
#define RCC_CLOCKTYPE_PCLK1 4U
#define RCC_CLOCKTYPE_PCLK2 8U
#define FLASH_LATENCY_0 0U
#define FLASH_LATENCY_1 1U
#define FLASH_LATENCY_2 2U
typedef struct { uint32_t PLLState,PLLSource,PLLMUL; } RCC_PLLInitTypeDef;
typedef struct { uint32_t PLL2State,PLL2MUL,HSEPrediv2Value; } RCC_PLL2InitTypeDef;
typedef struct { uint32_t OscillatorType,HSEState,HSIState,HSICalibrationValue,HSEPredivValue,Prediv1Source;
                 RCC_PLLInitTypeDef PLL;RCC_PLL2InitTypeDef PLL2; } RCC_OscInitTypeDef;
typedef struct { uint32_t ClockType,SYSCLKSource,AHBCLKDivider,APB1CLKDivider,APB2CLKDivider; } RCC_ClkInitTypeDef;
extern unsigned fake_sys_source,fake_pll_ready;
#define __HAL_RCC_GET_SYSCLK_SOURCE() (fake_sys_source)
#define __HAL_RCC_GET_FLAG(flag) ((void)(flag),fake_pll_ready)
uint32_t HAL_RCC_GetSysClockFreq(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_RCC_GetPCLK2Freq(void);
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *c);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *c,uint32_t latency);
#endif
