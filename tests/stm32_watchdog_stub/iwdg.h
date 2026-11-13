#ifndef TEST_STM32_IWDG_H
#define TEST_STM32_IWDG_H
#include <stdint.h>
typedef struct { uint32_t Prescaler, Reload; } IWDG_InitTypeDef;
typedef struct { void *Instance; IWDG_InitTypeDef Init; } IWDG_HandleTypeDef;
extern IWDG_HandleTypeDef hiwdg;
#define IWDG ((void *)0x40003000U)
#define HAL_OK 0
#define IWDG_PRESCALER_4 0U
#define IWDG_PRESCALER_8 1U
#define IWDG_PRESCALER_16 2U
#define IWDG_PRESCALER_32 3U
#define IWDG_PRESCALER_64 4U
#define IWDG_PRESCALER_128 5U
#define IWDG_PRESCALER_256 6U
int HAL_IWDG_Init(IWDG_HandleTypeDef *);
int HAL_IWDG_Refresh(IWDG_HandleTypeDef *);
void test_freeze(void);
#define __HAL_DBGMCU_FREEZE_IWDG() test_freeze()
#endif
