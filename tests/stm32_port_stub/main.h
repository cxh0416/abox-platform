#ifndef TEST_STM32_PORT_MAIN_H
#define TEST_STM32_PORT_MAIN_H
#include <stdint.h>
typedef enum { HAL_OK=0, HAL_ERROR=1, HAL_BUSY=2, HAL_TIMEOUT=3 } HAL_StatusTypeDef;
typedef struct { volatile uint32_t CR; } TestFlash;
extern TestFlash test_flash;
#define FLASH (&test_flash)
#define FLASH_CR_LOCK 0x80U
#define FLASH_TYPEPROGRAM_WORD 2U
#define FLASH_TYPEPROGRAM_HALFWORD 1U
#define FLASH_TYPEERASE_PAGES 0U
typedef struct { uint32_t TypeErase, Banks, PageAddress, NbPages; } FLASH_EraseInitTypeDef;
typedef struct { int unused; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart1;
#define EN_3_8V_GPIO_Port ((void*)0)
#define EN_3_8V_Pin 1U
#define EC_PWR_GPIO_Port ((void*)0)
#define EC_PWR_Pin 2U
#define GPIO_PIN_SET 1U
#define GPIO_PIN_RESET 0U
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t, uint32_t, uint64_t);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *, uint32_t *);
void HAL_GPIO_WritePin(void *, uint16_t, unsigned);
#endif
