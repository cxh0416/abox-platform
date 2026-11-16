#ifndef TEST_ADC_STM_HAL_H
#define TEST_ADC_STM_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { unsigned id; uint32_t CR2, DR, CR1, HTR, LTR; } ADC_TypeDef;
typedef struct { uint32_t ScanConvMode,ContinuousConvMode,DiscontinuousConvMode,ExternalTrigConv,DataAlign,NbrOfConversion; } ADC_InitTypeDef;
typedef struct { ADC_TypeDef *Instance; ADC_InitTypeDef Init; } ADC_HandleTypeDef;
typedef struct { uint32_t Channel,Rank,SamplingTime; } ADC_ChannelConfTypeDef;
typedef struct { uint32_t Mode; } ADC_MultiModeTypeDef;
typedef struct { uint32_t InjectedChannel,InjectedRank,InjectedSamplingTime,InjectedOffset,InjectedNbrOfConversion;
                 uint32_t InjectedDiscontinuousConvMode,AutoInjectedConv,ExternalTrigInjecConv; } ADC_InjectionConfTypeDef;
typedef struct { uint32_t PeriphClockSelection,AdcClockSelection; } RCC_PeriphCLKInitTypeDef;
extern ADC_TypeDef fake_adc_st[2];
#define ADC1 (&fake_adc_st[0])
#define ADC2 (&fake_adc_st[1])
#define RCC_PERIPHCLK_ADC 1U
#define RCC_ADCPCLK2_DIV2 2U
#define RCC_ADCPCLK2_DIV4 4U
#define RCC_ADCPCLK2_DIV6 6U
#define RCC_ADCPCLK2_DIV8 8U
#define ADC_SCAN_DISABLE 0U
#define ADC_SCAN_ENABLE 1U
#define ENABLE 1U
#define ADC_CR2_DMA (1U<<8)
#define ADC_CR2_ADON (1U<<0)
#define ADC_CR2_CONT (1U<<1)
#define ADC_CR2_EXTTRIG (1U<<20)
#define ADC_CR2_JEXTTRIG (1U<<15)
#define ADC_CR2_JSWSTART (1U<<21)
#define ADC_CR1_JAUTO (1U<<10)
#define ADC_CR1_JDISCEN (1U<<12)
#define ADC_CR1_JEOCIE (1U<<7)
#define ADC_FLAG_JEOC (1U<<2)
#define ADC_FLAG_AWD (1U<<0)
#define ADC_CR1_AWDCH 0x1fU
#define ADC_CR1_AWDIE (1U<<6)
#define ADC_CR1_AWDSGL (1U<<9)
#define ADC_CR1_JAWDEN (1U<<22)
#define ADC_CR1_AWDEN (1U<<23)
#define __HAL_ADC_GET_FLAG(h,f) fake_adc_injected_flag((h),(f))
#define __HAL_ADC_CLEAR_FLAG(h,f) fake_adc_clear_flag((h),(f))
#define DISABLE 0U
#define ADC_SOFTWARE_START 0U
#define ADC_DATAALIGN_RIGHT 0U
#define ADC_REGULAR_RANK_1 1U
#define ADC_MODE_INDEPENDENT 0U
#define ADC_DUALMODE_REGSIMULT (6U<<16U)
#define ADC_DUALMODE_INTERLFAST (7U<<16U)
#define ADC_DUALMODE_INTERLSLOW (8U<<16U)
#define __HAL_RCC_ADC1_CLK_ENABLE() fake_adc_clock(1,1)
#define __HAL_RCC_ADC2_CLK_ENABLE() fake_adc_clock(2,1)
#define __HAL_RCC_ADC1_CLK_DISABLE() fake_adc_clock(1,0)
#define __HAL_RCC_ADC2_CLK_DISABLE() fake_adc_clock(2,0)
void fake_adc_clock(unsigned id,unsigned enabled);
uint32_t HAL_RCC_GetPCLK2Freq(void);
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *p);
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h,ADC_ChannelConfTypeDef *c);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h,uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h);
uint32_t fake_adc_injected_flag(ADC_HandleTypeDef *h,uint32_t flag);
void fake_adc_clear_flag(ADC_HandleTypeDef *h,uint32_t flag);
HAL_StatusTypeDef HAL_ADCEx_InjectedConfigChannel(ADC_HandleTypeDef *h,ADC_InjectionConfTypeDef *c);
HAL_StatusTypeDef HAL_ADCEx_InjectedStart(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADCEx_InjectedStop(ADC_HandleTypeDef *h);
uint32_t HAL_ADCEx_InjectedGetValue(ADC_HandleTypeDef *h,uint32_t rank);
HAL_StatusTypeDef HAL_ADCEx_MultiModeConfigChannel(ADC_HandleTypeDef *h,ADC_MultiModeTypeDef *mode);
uint32_t HAL_ADCEx_MultiModeGetValue(ADC_HandleTypeDef *h);
#endif
