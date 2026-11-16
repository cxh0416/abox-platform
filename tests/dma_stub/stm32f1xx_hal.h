#ifndef TEST_DMA_STM_HAL_H
#define TEST_DMA_STM_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t CNDTR; unsigned index; } DMA_Channel_TypeDef;
typedef struct { uint32_t Direction,PeriphInc,MemInc,PeriphDataAlignment,MemDataAlignment,Mode,Priority; } DMA_InitTypeDef;
typedef struct __DMA_HandleTypeDef {
    DMA_Channel_TypeDef *Instance;
    DMA_InitTypeDef Init;
    void *Parent;
    void (*XferHalfCpltCallback)(struct __DMA_HandleTypeDef *);
    void (*XferCpltCallback)(struct __DMA_HandleTypeDef *);
    void (*XferErrorCallback)(struct __DMA_HandleTypeDef *);
} DMA_HandleTypeDef;
extern DMA_Channel_TypeDef fake_dma_st[12];
extern unsigned fake_dma_flags;
#define DMA1_Channel1 (&fake_dma_st[0])
#define DMA1_Channel2 (&fake_dma_st[1])
#define DMA1_Channel3 (&fake_dma_st[2])
#define DMA1_Channel4 (&fake_dma_st[3])
#define DMA1_Channel5 (&fake_dma_st[4])
#define DMA1_Channel6 (&fake_dma_st[5])
#define DMA1_Channel7 (&fake_dma_st[6])
#define DMA2_Channel1 (&fake_dma_st[7])
#define DMA2_Channel2 (&fake_dma_st[8])
#define DMA2_Channel3 (&fake_dma_st[9])
#define DMA2_Channel4 (&fake_dma_st[10])
#define DMA2_Channel5 (&fake_dma_st[11])
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_MEMORY_TO_PERIPH 1U
#define DMA_MEMORY_TO_MEMORY 2U
#define DMA_PINC_ENABLE 1U
#define DMA_PINC_DISABLE 0U
#define DMA_MINC_ENABLE 1U
#define DMA_MINC_DISABLE 0U
#define DMA_PDATAALIGN_BYTE 1U
#define DMA_PDATAALIGN_HALFWORD 2U
#define DMA_PDATAALIGN_WORD 4U
#define DMA_MDATAALIGN_BYTE 1U
#define DMA_MDATAALIGN_HALFWORD 2U
#define DMA_MDATAALIGN_WORD 4U
#define DMA_CIRCULAR 1U
#define DMA_NORMAL 0U
#define DMA_CCR_PL_Pos 12U
#define __HAL_RCC_DMA1_CLK_ENABLE() fake_dma_clock(1)
#define __HAL_RCC_DMA2_CLK_ENABLE() fake_dma_clock(2)
#define __HAL_DMA_GET_COUNTER(h) ((h)->Instance->CNDTR)
#define __HAL_DMA_GET_TE_FLAG_INDEX(h) ((void)(h),4U)
#define __HAL_DMA_GET_FLAG(h,f) ((void)(h), fake_dma_flags & (f))
void fake_dma_clock(unsigned controller);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *h);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *h, uint32_t source, uint32_t dest, uint32_t count);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *h);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h);
#endif
