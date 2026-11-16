#include "abox_f105_uart_hw.h"
#include "gd32f10x.h"
_Static_assert(USART_CTL1_LMEN==(1U<<14) && USART_CTL1_CKEN==(1U<<11) &&
               USART_CTL1_LBDIE==(1U<<6), "UART mode control layout");
_Static_assert(USART_CTL2_SCEN==(1U<<5) && USART_CTL2_NKEN==(1U<<4) &&
               USART_CTL2_HDEN==(1U<<3) && USART_CTL2_IREN==(1U<<1), "UART protocol layout");
ABoxF105UartRegisters *ABoxF105Uart_Registers(unsigned id)
{
    static const uint32_t reg[] = {USART0, USART1, USART2, UART3, UART4};
    return id >= 1U && id <= 5U ? (ABoxF105UartRegisters *)(uintptr_t)reg[id - 1U] : 0;
}
uint32_t ABoxF105Uart_ClockHz(unsigned id)
{ return rcu_clock_freq_get(id == 1U ? CK_APB2 : CK_APB1); }
void ABoxF105Uart_EnableClock(unsigned id)
{
    static const rcu_periph_enum clocks[] = {RCU_USART0, RCU_USART1, RCU_USART2, RCU_UART3, RCU_UART4};
    if (id >= 1U && id <= 5U) rcu_periph_clock_enable(clocks[id - 1U]);
}
ABoxF105DmaChannel *ABoxF105Dma_Channel(unsigned controller, unsigned channel)
{
    uintptr_t base;
    if (controller < 1U || controller > 2U || !channel || channel > (controller == 1U ? 7U : 5U)) return 0;
    base = controller == 1U ? (uintptr_t)&DMA_CHCTL(DMA0, DMA_CH0) : (uintptr_t)&DMA_CHCTL(DMA1, DMA_CH0);
    return (ABoxF105DmaChannel *)(base + (channel - 1U) * 0x14U);
}
void ABoxF105Dma_EnableClock(unsigned controller)
{ if (controller == 1U) rcu_periph_clock_enable(RCU_DMA0); else if (controller == 2U) rcu_periph_clock_enable(RCU_DMA1); }
uint32_t ABoxF105Dma_Flags(unsigned controller, unsigned channel)
{ return (DMA_INTF(controller == 1U ? DMA0 : DMA1) >> (4U * (channel - 1U))) & 0xFU; }
void ABoxF105Dma_Clear(unsigned controller, unsigned channel, uint32_t flags)
{ DMA_INTC(controller == 1U ? DMA0 : DMA1) = (flags & 0xFU) << (4U * (channel - 1U)); }
