#include <assert.h>
#include "main.h"
#include "usart.h"
#include "abox_board.h"
#include "abox_modem_port.h"
#include "abox_serial_port.h"
#if defined(ABOX_CAN_PORT_INTEGRATION)
#include <string.h>
#include "can.h"
#include "abox_can_port.h"
#endif

TestScb test_scb;
static unsigned step, init_ok = 1U, bind_ok = 1U, rx_count, modem_count, serial_errors, modem_errors, tx_count, ticks;
static uint8_t rx_buffer[8];
void __DSB(void) { assert(test_scb.VTOR == 0x08008000U && step++ == 0U); }
void __ISB(void) { assert(step++ == 1U); }
int HAL_Init(void) { assert(step++ == 2U); return init_ok ? HAL_OK : 1; }
void __enable_irq(void) { assert(step++ == 3U); }
int HAL_RCC_OscConfig(RCC_OscInitTypeDef *o) { assert(step++ == 4U && o->PLL.PLLMUL == 9U); return HAL_OK; }
int HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *c, uint32_t latency) { assert(step++ == 5U && c->APB1CLKDivider == 2U && latency == 2U); return HAL_OK; }
void test_plli2s_enable(void) { assert(step++ == 6U); }
void MX_GPIO_Init(void) { assert(step++ == 7U); }
void ABoxBoardIoStm32_InputsInit(void) { assert(step++ == 8U); }
void MX_DMA_Init(void) { assert(step++ == 9U); }
#if defined(ABOX_CAN_PORT_ENABLED)
#define CAN_STEPS 0U
void MX_CAN1_Init(void) { assert(!"legacy CAN1 must not start"); }
void MX_CAN2_Init(void) { assert(!"legacy CAN2 must not start"); }
#else
#define CAN_STEPS 2U
void MX_CAN1_Init(void) { assert(step++ == 10U); }
void MX_CAN2_Init(void) { assert(step++ == 11U); }
#endif
void MX_USART1_UART_Init(void) { assert(step++ == 10U + CAN_STEPS); }
void MX_UART5_Init(void) { assert(step++ == 11U + CAN_STEPS); }
int ABoxStm32F105Ec800Port_TryInit(void) { assert(step++ == 12U + CAN_STEPS); return bind_ok; }
uint8_t *ABoxStm32Serial_RxBuffer(void) { return rx_buffer; }
void ABoxModemPort_NotifyRx(uint16_t size) { assert(size == 3U); ++modem_count; }
void ABoxSerialPort_NotifyRx(const uint8_t *buffer, uint16_t size) { assert(buffer == rx_buffer && size == 3U); ++rx_count; }
void ABoxSerialPort_NotifyTxComplete(void) { ++tx_count; }
void ABoxModemPort_NotifyError(void) { ++modem_errors; }
void ABoxSerialPort_NotifyError(void) { ++serial_errors; }
void ABoxBoard_ModemUartError(uint32_t error, uint32_t state) { assert(error == 12U && state == 42U); }
void ABoxBoard_SerialUartError(uint32_t error) { assert(error == 12U); }
uint32_t HAL_UART_GetState(UART_HandleTypeDef *u) { (void)u; return 42U; }
void HAL_IncTick(void) { ++ticks; }
#if defined(ABOX_CAN_PORT_INTEGRATION)
CAN_HandleTypeDef hcan1, hcan2;
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 36000000U; }
uint32_t HAL_CAN_Init(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_READY; return HAL_OK; }
uint32_t HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f)
{ (void)h; assert(f->SlaveStartFilterBank == 14U); return HAL_OK; }
uint32_t HAL_CAN_Start(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_LISTENING; return HAL_OK; }
uint32_t HAL_CAN_Stop(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_READY; return HAL_OK; }
uint32_t HAL_CAN_DeInit(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_RESET; return HAL_OK; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) { (void)h; return 1U; }
uint32_t HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *header,
                              uint8_t data[8], uint32_t *mailbox)
{ (void)h; (void)header; (void)data; *mailbox = 0U; return HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo)
{ (void)h; (void)fifo; return 0U; }
uint32_t HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo,
                             CAN_RxHeaderTypeDef *header, uint8_t data[8])
{ (void)h; (void)fifo; (void)header; (void)data; return HAL_ERROR; }
#endif
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *, uint16_t);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *);
int main(void)
{
    UART_HandleTypeDef modem = {USART1, 12U}, serial = {UART5, 12U};
    TIM_HandleTypeDef timer = {TIM1};
    assert(ABoxBoard_Init(0x08008000U) && step == 13U + CAN_STEPS);
#if defined(ABOX_CAN_PORT_INTEGRATION)
    /* Board startup must leave the real public-port handles available. */
    assert(hcan1.State == HAL_CAN_STATE_RESET && hcan2.State == HAL_CAN_STATE_RESET);
    assert(ABoxCanPort_Init(0U, 250000U, ABOX_CAN_SILENT_LOOPBACK));
    assert(ABoxCanPort_Init(1U, 500000U, ABOX_CAN_SILENT_LOOPBACK));
    assert(!ABoxCanPort_Stop(0U));
    assert(ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
#endif
    step = 0U; bind_ok = 0U; assert(!ABoxBoard_Init(0x08008000U) && step == 13U + CAN_STEPS);
    step = 0U; init_ok = 0U; assert(!ABoxBoard_Init(0x08008000U) && step == 3U);
    HAL_UARTEx_RxEventCallback(&modem, 3U); HAL_UARTEx_RxEventCallback(&serial, 3U);
    HAL_UART_TxCpltCallback(&serial); HAL_UART_ErrorCallback(&modem); HAL_UART_ErrorCallback(&serial);
    HAL_TIM_PeriodElapsedCallback(&timer);
    assert(modem_count == 1U && tx_count == 1U && modem_errors == 1U && ticks == 1U);
#if defined(ABOX_SERIAL_RX_DISABLED) && ABOX_SERIAL_RX_DISABLED
    assert(rx_count == 0U && serial_errors == 0U);
#else
    assert(rx_count == 1U && serial_errors == 1U);
#endif
    return 0;
}
