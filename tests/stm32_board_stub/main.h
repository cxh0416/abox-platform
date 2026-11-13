#ifndef ABOX_BOARD_TEST_MAIN_H
#define ABOX_BOARD_TEST_MAIN_H
#include <stdint.h>
typedef struct { uint32_t PLLState, PLLSource, PLLMUL; } TestPll;
typedef struct { uint32_t PLL2State; } TestPll2;
typedef struct { uint32_t OscillatorType,HSEState,HSEPredivValue,HSIState,Prediv1Source; TestPll PLL; TestPll2 PLL2; } RCC_OscInitTypeDef;
typedef struct { uint32_t ClockType,SYSCLKSource,AHBCLKDivider,APB1CLKDivider,APB2CLKDivider; } RCC_ClkInitTypeDef;
typedef struct { uint32_t VTOR; } TestScb;
extern TestScb test_scb;
#define SCB (&test_scb)
#ifndef HAL_OK
#define HAL_OK 0
#endif
#define RCC_OSCILLATORTYPE_HSE 1
#define RCC_HSE_ON 1
#define RCC_HSE_PREDIV_DIV1 1
#define RCC_HSI_ON 1
#define RCC_PREDIV1_SOURCE_HSE 1
#define RCC_PLL_ON 1
#define RCC_PLLSOURCE_HSE 1
#define RCC_PLL_MUL9 9
#define RCC_PLL_NONE 0
#define RCC_CLOCKTYPE_HCLK 1
#define RCC_CLOCKTYPE_SYSCLK 2
#define RCC_CLOCKTYPE_PCLK1 4
#define RCC_CLOCKTYPE_PCLK2 8
#define RCC_SYSCLKSOURCE_PLLCLK 1
#define RCC_SYSCLK_DIV1 1
#define RCC_HCLK_DIV2 2
#define RCC_HCLK_DIV1 1
#define FLASH_LATENCY_2 2
#define TIM1 ((void *)0x100U)
typedef struct { void *Instance; } TIM_HandleTypeDef;
void __DSB(void);
void __ISB(void);
void __enable_irq(void);
void test_plli2s_enable(void);
#define __HAL_RCC_PLLI2S_ENABLE() test_plli2s_enable()
int HAL_Init(void);
int HAL_RCC_OscConfig(RCC_OscInitTypeDef *);
int HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *, uint32_t);
void HAL_IncTick(void);
#endif
