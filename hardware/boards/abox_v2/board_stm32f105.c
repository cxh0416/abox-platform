#include "abox_board.h"
#include "abox_modem_port.h"
#include "abox_serial_port.h"
#include "abox_board_io_stm32f105.h"
#include "abox_stm32f105_ec800_port.h"
#include "can.h"
#include "dma.h"
#include "gpio.h"
#include "main.h"
#include "usart.h"

uint8_t *ABoxStm32Serial_RxBuffer(void);
__attribute__((weak)) void ABoxBoard_ModemUartError(uint32_t error, uint32_t state)
{ (void)error; (void)state; }
__attribute__((weak)) void ABoxBoard_SerialUartError(uint32_t error) { (void)error; }

static int configure_clock(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clock = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.Prediv1Source = RCC_PREDIV1_SOURCE_HSE;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    osc.PLL2.PLL2State = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return 0;
    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV2;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_2) != HAL_OK) return 0;
    __HAL_RCC_PLLI2S_ENABLE();
    return 1;
}

int ABoxBoard_Init(uint32_t vector_address)
{
    SCB->VTOR = vector_address;
    __DSB();
    __ISB();
    if (HAL_Init() != HAL_OK) return 0;
    __enable_irq();
    if (!configure_clock()) return 0;
    MX_GPIO_Init();
    ABoxBoardIoStm32_InputsInit();
    MX_DMA_Init();
#if !defined(ABOX_CAN_PORT_ENABLED)
    /* Preserve legacy products. Public CAN Port owns initialization when
     * explicitly attached, avoiding an already-running HAL controller. */
    MX_CAN1_Init();
    MX_CAN2_Init();
#endif
    MX_USART1_UART_Init();
    MX_UART5_Init();
    return ABoxStm32F105Ec800Port_TryInit();
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    if (uart->Instance == USART1) ABoxModemPort_NotifyRx(size);
#if !defined(ABOX_SERIAL_RX_DISABLED) || !ABOX_SERIAL_RX_DISABLED
    else if (uart->Instance == UART5) ABoxSerialPort_NotifyRx(ABoxStm32Serial_RxBuffer(), size);
#endif
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == UART5) ABoxSerialPort_NotifyTxComplete();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART1) {
        ABoxModemPort_NotifyError();
        ABoxBoard_ModemUartError(uart->ErrorCode, (uint32_t)HAL_UART_GetState(uart));
    }
#if !defined(ABOX_SERIAL_RX_DISABLED) || !ABOX_SERIAL_RX_DISABLED
    else if (uart->Instance == UART5) {
        ABoxBoard_SerialUartError(uart->ErrorCode);
        ABoxSerialPort_NotifyError();
    }
#endif
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if (timer->Instance == TIM1) HAL_IncTick();
}
