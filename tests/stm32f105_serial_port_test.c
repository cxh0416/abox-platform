#include <assert.h>
#include "usart.h"
#include "abox_serial_port.h"

UART_HandleTypeDef huart5;
static uint32_t primask;
static int receive_result;
uint8_t *ABoxStm32Serial_RxBuffer(void);
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1U; }
void __set_PRIMASK(uint32_t key) { primask = key; }
int HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *h, uint8_t *data, uint16_t size)
{ assert(h == &huart5 && data && size && primask == 1U); return receive_result; }
int HAL_UART_DeInit(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
int HAL_UART_Init(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
int HAL_UART_AbortReceive(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
int HAL_UART_Transmit_IT(UART_HandleTypeDef *h, uint8_t *p, uint16_t n)
{ (void)h; (void)p; (void)n; return HAL_OK; }
int HAL_UART_AbortTransmit(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
int HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *p, uint16_t n, uint32_t timeout)
{ (void)h; (void)p; (void)n; (void)timeout; return HAL_OK; }
int main(void)
{
    uint8_t first[8], second[8];
    assert(!ABoxSerialPort_StartRx(0, sizeof(first)));
    assert(ABoxSerialPort_StartRx(first, sizeof(first)));
    assert(ABoxStm32Serial_RxBuffer() == first && primask == 0U);
    receive_result = 1; primask = 1U;
    assert(!ABoxSerialPort_StartRx(second, sizeof(second)));
    assert(ABoxStm32Serial_RxBuffer() == first && primask == 1U);
    receive_result = HAL_OK;
    assert(ABoxSerialPort_StartRx(second, sizeof(second)));
    assert(ABoxStm32Serial_RxBuffer() == second && primask == 1U);
    return 0;
}
