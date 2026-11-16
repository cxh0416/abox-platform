#include "abox_crc_backend.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>
_Static_assert(offsetof(CRC_TypeDef,DR)==0U && offsetof(CRC_TypeDef,IDR)==4U &&
               offsetof(CRC_TypeDef,CR)==8U && CRC_CR_RESET==1U,"STM32 CRC layout");
void ABoxCrcHw_Enable(void) { __HAL_RCC_CRC_CLK_ENABLE(); }
void ABoxCrcHw_Reset(void) { CRC->CR=CRC_CR_RESET; }
void ABoxCrcHw_WriteWord(uint32_t word) { CRC->DR=word; }
uint32_t ABoxCrcHw_Read(void) { return CRC->DR; }
void ABoxCrcHw_WriteScratch(uint8_t value) { *(volatile uint32_t *)((uintptr_t)CRC+4U)=value; }
uint8_t ABoxCrcHw_ReadScratch(void) { return (uint8_t)*(volatile uint32_t *)((uintptr_t)CRC+4U); }
