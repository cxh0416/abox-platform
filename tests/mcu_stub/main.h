#ifndef TEST_MCU_STM32_H
#define TEST_MCU_STM32_H
#include <stdint.h>
#define RCC_FLAG_PINRST 0U
#define RCC_FLAG_PORRST 1U
#define RCC_FLAG_IWDGRST 2U
#define RCC_FLAG_WWDGRST 3U
#define RCC_FLAG_SFTRST 4U
#define RCC_FLAG_LPWRRST 5U
extern uint32_t test_reset_flags;
#define __HAL_RCC_GET_FLAG(flag) ((test_reset_flags >> (flag)) & 1U)
#define __HAL_RCC_CLEAR_RESET_FLAGS() (test_reset_flags = 0U)
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
uint32_t HAL_GetUIDw2(void);
#endif
