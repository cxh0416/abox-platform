#ifndef STM32F1XX_HAL_DAC_H
#define STM32F1XX_HAL_DAC_H
#include "stm32f1xx_hal.h"
typedef struct { DAC_TypeDef *Instance; } DAC_HandleTypeDef;
typedef struct { uint32_t DAC_Trigger,DAC_OutputBuffer; } DAC_ChannelConfTypeDef;
#define DAC_CHANNEL_1 0U
#define DAC_CHANNEL_2 16U
#define DAC_ALIGN_12B_R 0U
#define DAC_OUTPUTBUFFER_ENABLE 0U
#define DAC_OUTPUTBUFFER_DISABLE 1U
#define DAC_TRIGGER_NONE 0U
#define DAC_TRIGGER_SOFTWARE 7U
#define DAC_TRIGGER_T2_TRGO 4U
#define DAC_TRIGGER_T3_TRGO 1U
#define DAC_TRIGGER_T4_TRGO 5U
#define DAC_TRIGGER_T5_TRGO 3U
#define DAC_TRIGGER_T6_TRGO 10U
#define DAC_TRIGGER_T7_TRGO 2U
#define DAC_TRIGGER_EXT_IT9 6U
int HAL_DAC_Init(DAC_HandleTypeDef *h);
int HAL_DAC_ConfigChannel(DAC_HandleTypeDef *h,const DAC_ChannelConfTypeDef *c,uint32_t ch);
int HAL_DAC_SetValue(DAC_HandleTypeDef *h,uint32_t ch,uint32_t align,uint32_t value);
int HAL_DACEx_TriangleWaveGenerate(DAC_HandleTypeDef *h,uint32_t ch,uint32_t amplitude);
int HAL_DACEx_NoiseWaveGenerate(DAC_HandleTypeDef *h,uint32_t ch,uint32_t amplitude);
uint32_t HAL_DAC_GetValue(DAC_HandleTypeDef *h,uint32_t ch);
#endif
