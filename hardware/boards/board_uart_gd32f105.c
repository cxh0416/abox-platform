#include "abox_board.h"
#include "gd32f10x.h"
int ABoxBoardUart_Prepare(ABoxUartId id)
{
    if (id == ABOX_BOARD_UART_MODEM) {
        rcu_periph_clock_enable(RCU_GPIOA);
        gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, 1U << 9);
        gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, 1U << 10);
        NVIC_SetPriority(USART0_IRQn, 0U); NVIC_EnableIRQ(USART0_IRQn);
        NVIC_SetPriority(DMA0_Channel3_IRQn, 0U); NVIC_EnableIRQ(DMA0_Channel3_IRQn);
        NVIC_SetPriority(DMA0_Channel4_IRQn, 0U); NVIC_EnableIRQ(DMA0_Channel4_IRQn);
    } else if (id == ABOX_BOARD_UART_BUS) {
        rcu_periph_clock_enable(RCU_GPIOC); rcu_periph_clock_enable(RCU_GPIOD);
        gpio_init(GPIOC, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, 1U << 12);
        gpio_init(GPIOD, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, 1U << 2);
        NVIC_SetPriority(UART4_IRQn, 0U); NVIC_EnableIRQ(UART4_IRQn);
    } else return 0;
    return 1;
}
