#include "abox_modem_port.h"
#include "usart.h"
int ABoxModemPort_AbortRx(void) { return HAL_UART_AbortReceive(&huart1) == HAL_OK; }
int ABoxModemPort_StartRx(uint8_t *buffer, uint16_t capacity)
{ return HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buffer, capacity) == HAL_OK; }
int ABoxModemPort_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{ return HAL_UART_Transmit(&huart1, (uint8_t *)data, length, timeout_ms) == HAL_OK; }
