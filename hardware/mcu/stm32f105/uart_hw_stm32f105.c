#include "abox_f105_uart_hw.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>
_Static_assert(offsetof(USART_TypeDef, GTPR)==offsetof(ABoxF105UartRegisters, guard), "UART register layout");
_Static_assert(USART_CR2_LINEN==(1U<<14) && USART_CR2_CLKEN==(1U<<11) &&
               USART_CR2_LBDIE==(1U<<6), "UART mode control layout");
_Static_assert(USART_CR3_SCEN==(1U<<5) && USART_CR3_NACK==(1U<<4) &&
               USART_CR3_HDSEL==(1U<<3) && USART_CR3_IREN==(1U<<1), "UART protocol layout");
ABoxF105UartRegisters *ABoxF105Uart_Registers(unsigned id)
{
    static USART_TypeDef *const reg[] = {USART1, USART2, USART3, UART4, UART5};
    return id >= 1U && id <= 5U ? (ABoxF105UartRegisters *)reg[id - 1U] : 0;
}
uint32_t ABoxF105Uart_ClockHz(unsigned id)
{ return id == 1U ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq(); }
void ABoxF105Uart_EnableClock(unsigned id)
{
    switch (id) {
    case 1U: __HAL_RCC_USART1_CLK_ENABLE(); break;
    case 2U: __HAL_RCC_USART2_CLK_ENABLE(); break;
    case 3U: __HAL_RCC_USART3_CLK_ENABLE(); break;
    case 4U: __HAL_RCC_UART4_CLK_ENABLE(); break;
    case 5U: __HAL_RCC_UART5_CLK_ENABLE(); break;
    default: break;
    }
}
ABoxF105DmaChannel *ABoxF105Dma_Channel(unsigned controller, unsigned channel)
{
    uintptr_t base;
    if (controller < 1U || controller > 2U || !channel || channel > (controller == 1U ? 7U : 5U)) return 0;
    base = controller == 1U ? (uintptr_t)DMA1_Channel1 : (uintptr_t)DMA2_Channel1;
    return (ABoxF105DmaChannel *)(base + (channel - 1U) * 0x14U);
}
void ABoxF105Dma_EnableClock(unsigned controller)
{ if (controller == 1U) __HAL_RCC_DMA1_CLK_ENABLE(); else if (controller == 2U) __HAL_RCC_DMA2_CLK_ENABLE(); }
uint32_t ABoxF105Dma_Flags(unsigned controller, unsigned channel)
{ DMA_TypeDef *d = controller == 1U ? DMA1 : DMA2; return (d->ISR >> (4U * (channel - 1U))) & 0xFU; }
void ABoxF105Dma_Clear(unsigned controller, unsigned channel, uint32_t flags)
{ DMA_TypeDef *d = controller == 1U ? DMA1 : DMA2; d->IFCR = (flags & 0xFU) << (4U * (channel - 1U)); }
