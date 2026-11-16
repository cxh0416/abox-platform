#include "abox_board.h"
#include "stm32f1xx_hal.h"
int ABoxBoardUart_Prepare(ABoxUartId id)
{
    GPIO_InitTypeDef gpio = {0};
    if (id == ABOX_BOARD_UART_MODEM) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_9; gpio.Mode = GPIO_MODE_AF_PP; gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Pin = GPIO_PIN_10; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
        HAL_NVIC_SetPriority(USART1_IRQn, 0U, 0U); HAL_NVIC_EnableIRQ(USART1_IRQn);
        HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 0U, 0U); HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
        HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0U, 0U); HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    } else if (id == ABOX_BOARD_UART_BUS) {
        __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_12; gpio.Mode = GPIO_MODE_AF_PP; gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOC, &gpio);
        gpio.Pin = GPIO_PIN_2; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOD, &gpio);
        HAL_NVIC_SetPriority(UART5_IRQn, 0U, 0U); HAL_NVIC_EnableIRQ(UART5_IRQn);
    } else return 0;
    return 1;
}
