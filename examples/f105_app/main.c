#include "abox_board.h"
#include "boot_cfg.h"
#if defined(ABOX_CAN_PORT_ENABLED)
#include "abox_can_port.h"
#endif

void Error_Handler(void) { for (;;) {} }

int main(void)
{
    const ABoxBoardUartConfig uarts[] = {
        {ABOX_BOARD_UART_MODEM, {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}},
        {ABOX_BOARD_UART_BUS, {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}}
    };
    const ABoxMcuClockConfig clock = {ABOX_MCU_CLOCK_PLL_HSE, 8000000U, 1U, 9U, 1U, 2U, 1U, 0U, 0U};
    const ABoxBoardConfig board = {APP_START_ADDR, uarts, sizeof(uarts) / sizeof(uarts[0]), &clock};
    if (!ABoxBoard_Init(&board)) Error_Handler();
#if defined(ABOX_CAN_PORT_ENABLED)
    /* Example uses silent loopback; real buses require an application contract. */
    const ABoxCanConfig can = {250000U, 15U, 2U, 1U, ABOX_CAN_SILENT_LOOPBACK, {1U, 0U, 1U}, {0}};
    if (!ABoxCanPort_Init(0U, &can)) Error_Handler();
#endif
    for (;;) {}
}
