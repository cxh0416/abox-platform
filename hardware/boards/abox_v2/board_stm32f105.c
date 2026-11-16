#include "abox_board.h"
#include "abox_board_io_stm32f105.h"
#include "abox_stm32f105_ec800_port.h"
#include "can.h"
#include "dma.h"
#include "gpio.h"
#include "main.h"
#include "usart.h"

__attribute__((weak)) void ABoxBoard_ModemUartError(uint32_t error, uint32_t state)
{ (void)error; (void)state; }
__attribute__((weak)) void ABoxBoard_SerialUartError(uint32_t error) { (void)error; }

int ABoxBoard_Init(const ABoxBoardConfig *config)
{
    if (!config || !config->clock || (config->uart_count && !config->uarts)) return 0;
    SCB->VTOR = config->vector_address;
    __DSB();
    __ISB();
    if (HAL_Init() != HAL_OK) return 0;
    __enable_irq();
    if (ABoxMcuClock_Apply(config->clock) != ABOX_HAL_OK) return 0;
    MX_GPIO_Init();
    ABoxBoardIoStm32_InputsInit();
    MX_DMA_Init();
    for (unsigned i = 0U; i < config->uart_count; ++i) {
        if (!ABoxBoardUart_Prepare(config->uarts[i].id) ||
            !ABoxUart_Open(config->uarts[i].id, &config->uarts[i].config)) return 0;
    }
    return ABoxStm32F105Ec800Port_TryInit();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if (timer->Instance == TIM1) HAL_IncTick();
}
