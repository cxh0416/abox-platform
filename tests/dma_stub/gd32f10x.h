#ifndef TEST_DMA_GD_H
#define TEST_DMA_GD_H
#include <stdint.h>
typedef enum { RESET = 0, SET = 1 } FlagStatus;
typedef enum { DMA_CH0=0,DMA_CH1,DMA_CH2,DMA_CH3,DMA_CH4,DMA_CH5,DMA_CH6 } dma_channel_enum;
typedef struct { uint32_t periph_addr,periph_width,memory_addr,memory_width,number,priority;
                 uint8_t periph_inc,memory_inc,direction; } dma_parameter_struct;
#define DMA0 1U
#define DMA1 2U
#define RCU_DMA0 1U
#define RCU_DMA1 2U
#define DMA_INT_FTF 1U
#define DMA_INT_HTF 2U
#define DMA_INT_ERR 4U
#define DMA_INT_FLAG_FTF 1U
#define DMA_INT_FLAG_HTF 2U
#define DMA_INT_FLAG_ERR 4U
#define DMA_FLAG_G 8U
#define DMA_PERIPHERAL_TO_MEMORY 0U
#define DMA_MEMORY_TO_PERIPHERAL 1U
#define DMA_PERIPH_INCREASE_ENABLE 1U
#define DMA_PERIPH_INCREASE_DISABLE 0U
#define DMA_MEMORY_INCREASE_ENABLE 1U
#define DMA_MEMORY_INCREASE_DISABLE 0U
#define DMA_PERIPHERAL_WIDTH_8BIT 1U
#define DMA_PERIPHERAL_WIDTH_16BIT 2U
#define DMA_PERIPHERAL_WIDTH_32BIT 4U
#define DMA_MEMORY_WIDTH_8BIT 1U
#define DMA_MEMORY_WIDTH_16BIT 2U
#define DMA_MEMORY_WIDTH_32BIT 4U
void rcu_periph_clock_enable(uint32_t controller);
void dma_deinit(uint32_t controller,dma_channel_enum channel);
void dma_struct_para_init(dma_parameter_struct *p);
void dma_init(uint32_t controller,dma_channel_enum channel,dma_parameter_struct *p);
void dma_circulation_enable(uint32_t controller,dma_channel_enum channel);
void dma_memory_to_memory_enable(uint32_t controller,dma_channel_enum channel);
void dma_flag_clear(uint32_t controller,dma_channel_enum channel,uint32_t flag);
void dma_interrupt_enable(uint32_t controller,dma_channel_enum channel,uint32_t flags);
void dma_interrupt_disable(uint32_t controller,dma_channel_enum channel,uint32_t flags);
void dma_channel_enable(uint32_t controller,dma_channel_enum channel);
void dma_channel_disable(uint32_t controller,dma_channel_enum channel);
uint32_t dma_transfer_number_get(uint32_t controller,dma_channel_enum channel);
FlagStatus dma_interrupt_flag_get(uint32_t controller,dma_channel_enum channel,uint32_t flag);
void dma_interrupt_flag_clear(uint32_t controller,dma_channel_enum channel,uint32_t flag);
#endif
