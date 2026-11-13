#include "abox_serial_port.h"

#include "usart.h"

static uint8_t *rx_buffer;

/* The buffer is owned by the caller until receive completes or aborts. */
uint8_t *ABoxStm32Serial_RxBuffer(void) { return rx_buffer; }

int ABoxSerialPort_IsConfig(uint32_t baud)
{
    return huart5.Init.BaudRate == baud && huart5.Init.Parity == UART_PARITY_NONE;
}

int ABoxSerialPort_Deinit(void) { return HAL_UART_DeInit(&huart5) == HAL_OK; }

void ABoxSerialPort_SetConfig(uint32_t baud)
{
    huart5.Init.BaudRate = baud;
    huart5.Init.Parity = UART_PARITY_NONE;
    huart5.Init.WordLength = UART_WORDLENGTH_8B;
    huart5.Init.StopBits = UART_STOPBITS_1;
}

int ABoxSerialPort_Init(void)
{ return huart5.Init.BaudRate != 0U && HAL_UART_Init(&huart5) == HAL_OK; }
int ABoxSerialPort_StartRx(uint8_t *data, uint16_t length)
{
    uint8_t *previous;
    uint32_t key;
    int ok;
    if (!data || !length) return 0;
    key = __get_PRIMASK();
    __disable_irq();
    previous = rx_buffer;
    rx_buffer = data;
    ok = HAL_UARTEx_ReceiveToIdle_IT(&huart5, data, length) == HAL_OK;
    if (!ok) rx_buffer = previous;
    __set_PRIMASK(key);
    return ok;
}
int ABoxSerialPort_AbortRx(void) { return HAL_UART_AbortReceive(&huart5) == HAL_OK; }
int ABoxSerialPort_Send(const uint8_t *data, uint16_t length)
{ return HAL_UART_Transmit_IT(&huart5, (uint8_t *)data, length) == HAL_OK; }
void ABoxSerialPort_AbortTx(void) { (void)HAL_UART_AbortTransmit(&huart5); }
int ABoxSerialPort_SendBlocking(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{ return HAL_UART_Transmit(&huart5, (uint8_t *)data, length, timeout_ms) == HAL_OK; }
