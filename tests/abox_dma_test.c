#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_dma.h"
#include "abox_dma_resource.h"
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t key) { (void)key; }

static unsigned clocks[3], total_events, seen_controller, seen_channel;
static ABoxDmaEvent seen_event;
static uintptr_t last_source, last_dest;
static uint16_t last_count;
static unsigned peripheral_increment, memory_increment, mode_m2m;
void fake_dma_clock(unsigned controller) { clocks[controller] = 1U; }
static void event(void *ctx, unsigned controller, unsigned channel, ABoxDmaEvent value)
{
    assert(ctx == (void *)0x1234); ++total_events;
    seen_controller=controller; seen_channel=channel; seen_event=value;
}

#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
DMA_Channel_TypeDef fake_dma_st[12];
unsigned fake_dma_flags;
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h)
{ peripheral_increment=h->Init.PeriphInc;memory_increment=h->Init.MemInc;mode_m2m=(h->Init.Direction==DMA_MEMORY_TO_MEMORY);return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *h,uint32_t src,uint32_t dest,uint32_t n)
{ last_source=src;last_dest=dest;last_count=(uint16_t)n;h->Instance->CNDTR=n;fake_dma_flags=0;return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *h) { (void)h;fake_dma_flags=0;return HAL_OK; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h)
{
    if(fake_dma_flags&2U){fake_dma_flags&=~2U;h->XferHalfCpltCallback(h);}
    else if(fake_dma_flags&1U){fake_dma_flags&=~1U;h->XferCpltCallback(h);}
}
static void raise_event(unsigned flags) { fake_dma_flags |= flags; }
#else
#include "gd32f10x.h"
static uint32_t flags_by_channel[2][7], enabled_by_channel[2][7], remaining_by_channel[2][7];
static int mem_to_periph;
void rcu_periph_clock_enable(uint32_t controller) { fake_dma_clock(controller); }
void dma_deinit(uint32_t d,dma_channel_enum c) { flags_by_channel[d-1U][c]=0;enabled_by_channel[d-1U][c]=0; }
void dma_struct_para_init(dma_parameter_struct *p) { memset(p,0,sizeof(*p)); }
void dma_init(uint32_t d,dma_channel_enum c,dma_parameter_struct *p)
{
    mem_to_periph=p->direction == DMA_MEMORY_TO_PERIPHERAL;
    last_source=mem_to_periph?p->memory_addr:p->periph_addr;
    last_dest=mem_to_periph?p->periph_addr:p->memory_addr;last_count=(uint16_t)p->number;
    peripheral_increment=p->periph_inc;memory_increment=p->memory_inc;remaining_by_channel[d-1U][c]=p->number;
}
void dma_circulation_enable(uint32_t d,dma_channel_enum c) { (void)d;(void)c; }
void dma_memory_to_memory_enable(uint32_t d,dma_channel_enum c) { (void)d;(void)c;mode_m2m=1; }
void dma_flag_clear(uint32_t d,dma_channel_enum c,uint32_t f) { (void)f;flags_by_channel[d-1U][c]=0; }
void dma_interrupt_enable(uint32_t d,dma_channel_enum c,uint32_t f) { enabled_by_channel[d-1U][c]|=f; }
void dma_interrupt_disable(uint32_t d,dma_channel_enum c,uint32_t f) { enabled_by_channel[d-1U][c]&=~f; }
void dma_channel_enable(uint32_t d,dma_channel_enum c) { (void)d;(void)c; }
void dma_channel_disable(uint32_t d,dma_channel_enum c) { (void)d;(void)c; }
uint32_t dma_transfer_number_get(uint32_t d,dma_channel_enum c) { return remaining_by_channel[d-1U][c]; }
FlagStatus dma_interrupt_flag_get(uint32_t d,dma_channel_enum c,uint32_t f)
{ return (flags_by_channel[d-1U][c]&enabled_by_channel[d-1U][c]&f)?SET:RESET; }
void dma_interrupt_flag_clear(uint32_t d,dma_channel_enum c,uint32_t f) { flags_by_channel[d-1U][c]&=~f; }
static void raise_event(unsigned flags) { flags_by_channel[0][1]|=flags; }
#endif

int main(void)
{
    ABoxDmaConfig c={ABOX_DMA_PERIPHERAL_TO_MEMORY,1U,2U,0U,1U,0U,2U};
    ABoxDmaEvents e={(void *)0x1234,event};
    uint16_t remaining=77U;
    assert(ABoxDma_Open(2,6,&c)==ABOX_HAL_INVALID);
    c.memory_width=3U; assert(ABoxDma_Open(1,2,&c)==ABOX_HAL_INVALID);c.memory_width=2U;
    assert(ABoxDma_Remaining(1,2,&remaining)==ABOX_HAL_INVALID && remaining==77U);
    assert(ABoxDmaResource_Claim(1U, 2U));
    assert(ABoxDma_Open(1,2,&c)==ABOX_HAL_BUSY);
    ABoxDmaResource_Release(1U, 2U);
    assert(ABoxDma_Open(1,2,&c)==ABOX_HAL_OK && clocks[1]);
    assert(!ABoxDmaResource_Claim(1U, 2U));
    assert(ABoxDma_Open(1,3,&c)==ABOX_HAL_OK && ABoxDma_Close(1,3)==ABOX_HAL_OK && clocks[1]);
    assert(ABoxDma_Open(1,2,&c)==ABOX_HAL_BUSY);
    assert(ABoxDma_Start(1,2,0x4000U,0x2001U,5U,&e)==ABOX_HAL_INVALID);
    assert(ABoxDma_Start(1,2,0x4000U,0xfffffffeU,2U,&e)==ABOX_HAL_INVALID);
    assert(ABoxDma_Start(1,2,0x4000U,0x2000U,0U,&e)==ABOX_HAL_INVALID);
    assert(ABoxDma_Start(1,2,0x4000U,0x2000U,5U,&e)==ABOX_HAL_OK);
    assert(last_source==0x4000U && last_dest==0x2000U && last_count==5U);
    assert(ABoxDma_Remaining(1,2,&remaining)==ABOX_HAL_OK && remaining==5U);
    assert(ABoxDma_Start(1,2,0x4000U,0x2000U,5U,&e)==ABOX_HAL_BUSY);
    assert(ABoxDma_Close(1,2)==ABOX_HAL_BUSY);
    raise_event(2U);ABoxDma_Irq(1,2);assert(total_events==1U && seen_event==ABOX_DMA_HALF);
    raise_event(1U);ABoxDma_Irq(1,2);assert(total_events==2U && seen_event==ABOX_DMA_COMPLETE && seen_controller==1U && seen_channel==2U);
    assert(ABoxDma_Start(1,2,0x4000U,0x2000U,5U,&e)==ABOX_HAL_OK);
    raise_event(7U);ABoxDma_Irq(1,2);assert(total_events==3U && seen_event==ABOX_DMA_TRANSFER_ERROR);
    assert(ABoxDma_Start(1,2,0x4000U,0x2000U,5U,&e)==ABOX_HAL_ERROR);
    assert(ABoxDma_Abort(1,2)==ABOX_HAL_OK && ABoxDma_Close(1,2)==ABOX_HAL_OK);
    c.circular=1U;assert(ABoxDma_Open(1,2,&c)==ABOX_HAL_OK);
    assert(ABoxDma_Start(1,2,0x4000U,0x2000U,5U,&e)==ABOX_HAL_OK);
    raise_event(1U);ABoxDma_Irq(1,2);assert(seen_event==ABOX_DMA_COMPLETE && ABoxDma_Close(1,2)==ABOX_HAL_BUSY);
    assert(ABoxDma_Abort(1,2)==ABOX_HAL_OK && ABoxDma_Close(1,2)==ABOX_HAL_OK);
    c.direction=ABOX_DMA_MEMORY_TO_MEMORY;assert(ABoxDma_Open(1,2,&c)==ABOX_HAL_INVALID);
    c.circular=0U;c.peripheral_width=4U;c.memory_width=4U;c.peripheral_increment=1U;
    mode_m2m=0;assert(ABoxDma_Open(1,2,&c)==ABOX_HAL_OK);
    assert(ABoxDma_Start(1,2,0x1000U,0x2000U,4U,&e)==ABOX_HAL_OK);
    assert(mode_m2m && peripheral_increment && memory_increment && last_source==0x1000U && last_dest==0x2000U);
    assert(ABoxDma_Abort(1,2)==ABOX_HAL_OK && ABoxDma_Close(1,2)==ABOX_HAL_OK);
    ABoxDma_Irq(3,1);
    return 0;
}
