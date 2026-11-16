#include <assert.h>
#include <stddef.h>
#include "main.h"
#include "usart.h"
#include "abox_board.h"
#if defined(ABOX_CAN_PORT_INTEGRATION)
#include <string.h>
#include "can.h"
#include "abox_can_port.h"
#endif

TestScb test_scb;
static unsigned step, init_ok = 1U, clock_ok = 1U, bind_ok = 1U, ticks;
void __DSB(void) { assert(test_scb.VTOR == 0x08008000U && step++ == 0U); }
void __ISB(void) { assert(step++ == 1U); }
int HAL_Init(void) { assert(step++ == 2U); return init_ok ? HAL_OK : 1; }
void __enable_irq(void) { assert(step++ == 3U); }
ABoxHalStatus ABoxMcuClock_Apply(const ABoxMcuClockConfig *c)
{ assert(step++ == 4U && c && c->source == ABOX_MCU_CLOCK_PLL_HSE && c->pll_multiplier == 9U && c->apb1_divider == 2U); return clock_ok ? ABOX_HAL_OK : ABOX_HAL_ERROR; }
void MX_GPIO_Init(void) { assert(step++ == 5U); }
void ABoxBoardIoStm32_InputsInit(void) { assert(step++ == 6U); }
void MX_DMA_Init(void) { assert(step++ == 7U); }
#define CAN_STEPS 0U
void MX_CAN1_Init(void) { assert(!"board must not start CAN implicitly"); }
void MX_CAN2_Init(void) { assert(!"board must not start CAN implicitly"); }
int ABoxBoardUart_Prepare(ABoxUartId id)
{ assert(id == (step == 8U + CAN_STEPS ? ABOX_BOARD_UART_MODEM : ABOX_BOARD_UART_BUS)); ++step; return 1; }
int ABoxUart_Open(ABoxUartId id, const ABoxUartConfig *config)
{ assert(config && config->baud == (id == ABOX_BOARD_UART_BUS ? 19200U : 115200U) && config->data_bits == 8U); return 1; }
int ABoxStm32F105Ec800Port_TryInit(void) { assert(step++ == 10U + CAN_STEPS); return bind_ok; }
void HAL_IncTick(void) { ++ticks; }
#if defined(ABOX_CAN_PORT_INTEGRATION)
CAN_HandleTypeDef hcan1, hcan2;
CAN_TypeDef test_can1, test_can2;
unsigned test_reset_force[2], test_reset_release[2];
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
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *);
int main(void)
{
    TIM_HandleTypeDef timer = {TIM1};
    const ABoxBoardUartConfig uarts[] = {
        {ABOX_BOARD_UART_MODEM, {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}},
        {ABOX_BOARD_UART_BUS, {19200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}}
    };
    const ABoxMcuClockConfig clock = {ABOX_MCU_CLOCK_PLL_HSE,8000000U,1U,9U,1U,2U,1U,0U,0U};
    const ABoxBoardConfig board = {0x08008000U, uarts, sizeof(uarts) / sizeof(uarts[0]), &clock};
    const ABoxBoardConfig invalid = {0x08008000U, 0, 1U, &clock};
    const ABoxBoardConfig no_clock = {0x08008000U, uarts, sizeof(uarts) / sizeof(uarts[0]), 0};
    assert(!ABoxBoard_Init(NULL));
    assert(!ABoxBoard_Init(&invalid));
    assert(!ABoxBoard_Init(&no_clock));
    assert(ABoxBoard_Init(&board) && step == 11U + CAN_STEPS);
#if defined(ABOX_CAN_PORT_INTEGRATION)
    ABoxCanConfig can = {250000U, 15U, 2U, 1U, ABOX_CAN_SILENT_LOOPBACK, {1U, 0U, 1U}, {0}};
    /* Board startup must leave the real public-port handles available. */
    assert(hcan1.State == HAL_CAN_STATE_RESET && hcan2.State == HAL_CAN_STATE_RESET);
    assert(ABoxCanPort_Init(0U, &can));
    can.bitrate = 500000U;
    assert(ABoxCanPort_Init(1U, &can));
    assert(!ABoxCanPort_Stop(0U));
    assert(ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
#endif
    step = 0U; bind_ok = 0U; assert(!ABoxBoard_Init(&board) && step == 11U + CAN_STEPS);
    step = 0U; init_ok = 0U; assert(!ABoxBoard_Init(&board) && step == 3U);
    step = 0U; init_ok = 1U; clock_ok = 0U; assert(!ABoxBoard_Init(&board) && step == 5U);
    HAL_TIM_PeriodElapsedCallback(&timer);
    assert(ticks == 1U);
    return 0;
}
