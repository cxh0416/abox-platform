#ifndef TEST_TIMER_STM_HAL_H
#define TEST_TIMER_STM_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t CNT, flags, irq, enabled, SMCR, CR1, CR2, CCER, BDTR, DIER, preload,RCR,EGR,DCR,DMAR; union { uint32_t capture[4]; struct { uint32_t CCR1,CCR2,CCR3,CCR4; }; }; unsigned id; } TIM_TypeDef;
typedef struct { uint32_t Prescaler,Period,CounterMode,ClockDivision,AutoReloadPreload,RepetitionCounter; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { uint32_t OCMode,Pulse,OCPolarity,OCNPolarity,OCFastMode,OCIdleState,OCNIdleState; } TIM_OC_InitTypeDef;
typedef struct { uint32_t ICPolarity,ICSelection,ICPrescaler,ICFilter; } TIM_IC_InitTypeDef;
typedef struct { uint32_t EncoderMode,IC1Polarity,IC2Polarity,IC1Selection,IC2Selection,IC1Prescaler,IC2Prescaler,IC1Filter,IC2Filter; } TIM_Encoder_InitTypeDef;
extern TIM_TypeDef fake_timer_st[7];
void fake_timer_reset(unsigned id,unsigned on);
#define __HAL_RCC_TIM1_FORCE_RESET() fake_timer_reset(1U,1U)
#define __HAL_RCC_TIM2_FORCE_RESET() fake_timer_reset(2U,1U)
#define __HAL_RCC_TIM3_FORCE_RESET() fake_timer_reset(3U,1U)
#define __HAL_RCC_TIM4_FORCE_RESET() fake_timer_reset(4U,1U)
#define __HAL_RCC_TIM5_FORCE_RESET() fake_timer_reset(5U,1U)
#define __HAL_RCC_TIM6_FORCE_RESET() fake_timer_reset(6U,1U)
#define __HAL_RCC_TIM7_FORCE_RESET() fake_timer_reset(7U,1U)
#define __HAL_RCC_TIM1_RELEASE_RESET() fake_timer_reset(1U,0U)
#define __HAL_RCC_TIM2_RELEASE_RESET() fake_timer_reset(2U,0U)
#define __HAL_RCC_TIM3_RELEASE_RESET() fake_timer_reset(3U,0U)
#define __HAL_RCC_TIM4_RELEASE_RESET() fake_timer_reset(4U,0U)
#define __HAL_RCC_TIM5_RELEASE_RESET() fake_timer_reset(5U,0U)
#define __HAL_RCC_TIM6_RELEASE_RESET() fake_timer_reset(6U,0U)
#define __HAL_RCC_TIM7_RELEASE_RESET() fake_timer_reset(7U,0U)
#define TIM1 (&fake_timer_st[0])
#define TIM2 (&fake_timer_st[1])
#define TIM3 (&fake_timer_st[2])
#define TIM4 (&fake_timer_st[3])
#define TIM5 (&fake_timer_st[4])
#define TIM6 (&fake_timer_st[5])
#define TIM7 (&fake_timer_st[6])
#define TIM_COUNTERMODE_UP 0U
#define TIM_COUNTERMODE_DOWN 1U
#define TIM_COUNTERMODE_CENTERALIGNED1 2U
#define TIM_COUNTERMODE_CENTERALIGNED2 3U
#define TIM_COUNTERMODE_CENTERALIGNED3 4U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0U
#define TIM_FLAG_UPDATE 1U
#define TIM_IT_UPDATE 1U
#define TIM_IT_CC1 2U
#define TIM_IT_BREAK (1U<<7)
#define TIM_FLAG_CC1 2U
#define TIM_FLAG_CC1OF 512U
#define TIM_FLAG_BREAK 128U
#define TIM_BDTR_MOE (1U<<15U)
#define TIM_CCER_CC1NE (1U<<2U)
#define TIM_CCER_CC1NP (1U<<3U)
#define TIM_DMA_CC1 (1U << 9U)
#define TIM_DMA_UPDATE (1U << 8U)
#define TIM_CR2_MMS (7U << 4U)
#define TIM_CR1_OPM (1U << 3U)
#define TIM_EGR_UG 1U
#define TIM_CR2_TI1S (1U << 7U)
#define TIM_SMCR_SMS 7U
#define TIM_SMCR_ECE 16384U
#define TIM_ICPSC_DIV1 0U
#define TIM_ICPSC_DIV2 4U
#define TIM_ICPSC_DIV4 8U
#define TIM_ICPSC_DIV8 12U
#define TIM_ICPOLARITY_RISING 0U
#define TIM_ICPOLARITY_FALLING 2U
#define TIM_ICSELECTION_DIRECTTI 1U
#define TIM_ICSELECTION_INDIRECTTI 2U
#define TIM_ICSELECTION_TRC 3U
#define TIM_ENCODERMODE_TI1 1U
#define TIM_ENCODERMODE_TI2 2U
#define TIM_ENCODERMODE_TI12 3U
#define TIM_ENCODERINPUTPOLARITY_RISING 0U
#define TIM_ENCODERINPUTPOLARITY_FALLING 2U
#define TIM_OCMODE_FORCED_INACTIVE 1U
#define TIM_OCMODE_FORCED_ACTIVE 2U
#define TIM_OCMODE_PWM1 3U
#define TIM_OCMODE_TIMING 4U
#define TIM_OCMODE_ACTIVE 5U
#define TIM_OCMODE_INACTIVE 6U
#define TIM_OCMODE_TOGGLE 7U
#define TIM_OCPOLARITY_LOW 1U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCNPOLARITY_HIGH 0U
#define TIM_OCFAST_DISABLE 0U
#define TIM_OCIDLESTATE_RESET 0U
#define TIM_OCNIDLESTATE_RESET 0U
#define TIM_CCx_ENABLE 1U
#define TIM_CCx_DISABLE 0U
void fake_timer_clock(unsigned id,unsigned on);
#define __HAL_RCC_TIM1_CLK_ENABLE() fake_timer_clock(1,1)
#define __HAL_RCC_TIM2_CLK_ENABLE() fake_timer_clock(2,1)
#define __HAL_RCC_TIM3_CLK_ENABLE() fake_timer_clock(3,1)
#define __HAL_RCC_TIM4_CLK_ENABLE() fake_timer_clock(4,1)
#define __HAL_RCC_TIM5_CLK_ENABLE() fake_timer_clock(5,1)
#define __HAL_RCC_TIM6_CLK_ENABLE() fake_timer_clock(6,1)
#define __HAL_RCC_TIM7_CLK_ENABLE() fake_timer_clock(7,1)
#define __HAL_RCC_TIM1_CLK_DISABLE() fake_timer_clock(1,0)
#define __HAL_RCC_TIM2_CLK_DISABLE() fake_timer_clock(2,0)
#define __HAL_RCC_TIM3_CLK_DISABLE() fake_timer_clock(3,0)
#define __HAL_RCC_TIM4_CLK_DISABLE() fake_timer_clock(4,0)
#define __HAL_RCC_TIM5_CLK_DISABLE() fake_timer_clock(5,0)
#define __HAL_RCC_TIM6_CLK_DISABLE() fake_timer_clock(6,0)
#define __HAL_RCC_TIM7_CLK_DISABLE() fake_timer_clock(7,0)
#define __HAL_TIM_CLEAR_FLAG(h,f) ((h)->Instance->flags &= ~(f))
#define __HAL_TIM_GET_COUNTER(h) ((h)->Instance->CNT)
#define __HAL_TIM_SET_COUNTER(h,v) ((h)->Instance->CNT=(v))
#define __HAL_TIM_GET_IT_SOURCE(h,f) ((h)->Instance->irq & (f))
#define __HAL_TIM_GET_FLAG(h,f) ((h)->Instance->flags & (f))
#define __HAL_TIM_CLEAR_IT(h,f) ((h)->Instance->flags &= ~(f))
#define __HAL_TIM_ENABLE_IT(h,f) ((h)->Instance->irq |= (f))
#define __HAL_TIM_DISABLE_IT(h,f) ((h)->Instance->irq &= ~(f))
#define __HAL_TIM_ENABLE_DMA(h,f) ((h)->Instance->DIER |= (f))
#define __HAL_TIM_DISABLE_DMA(h,f) ((h)->Instance->DIER &= ~(f))
#define __HAL_TIM_ENABLE_OCxPRELOAD(h,c) ((h)->Instance->preload |= (1U << ((c)/4U)))
#define __HAL_TIM_DISABLE_OCxPRELOAD(h,c) ((h)->Instance->preload &= ~(1U << ((c)/4U)))
#define __HAL_TIM_MOE_ENABLE(h) ((h)->Instance->BDTR |= TIM_BDTR_MOE)
#define __HAL_TIM_MOE_DISABLE(h) ((h)->Instance->BDTR &= ~TIM_BDTR_MOE)
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_Base_DeInit(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_OC_ConfigChannel(TIM_HandleTypeDef *h,const TIM_OC_InitTypeDef *c,uint32_t channel);
HAL_StatusTypeDef HAL_TIM_IC_ConfigChannel(TIM_HandleTypeDef *h,const TIM_IC_InitTypeDef *c,uint32_t channel);
HAL_StatusTypeDef HAL_TIM_Encoder_Init(TIM_HandleTypeDef *h,const TIM_Encoder_InitTypeDef *c);
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *h,uint32_t channel);
void TIM_CCxChannelCmd(TIM_TypeDef *t,uint32_t channel,uint32_t enabled);
#endif
