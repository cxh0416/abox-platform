#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H
#include <stdint.h>
typedef struct { uint32_t CR, SWTRIGR, DHR12R1, DHR12L1, DHR8R1, DHR12R2; } DAC_TypeDef;
extern DAC_TypeDef dac_registers;
#define DAC (&dac_registers)
#define DAC_CR_DMAEN1 (1U<<12)
#define DAC_SWTRIGR_SWTRIG1 1U
#define DAC_SWTRIGR_SWTRIG2 2U
#define HAL_OK 0
#define HAL_ERROR 1
extern unsigned clocks,enabled,disabled;
#define __HAL_RCC_DAC_CLK_ENABLE() (++clocks)
#define __HAL_DAC_ENABLE(h,ch) (enabled |= 1U<<((ch)==0U?0U:1U))
#define __HAL_DAC_DISABLE(h,ch) (disabled |= 1U<<((ch)==0U?0U:1U))
#endif
