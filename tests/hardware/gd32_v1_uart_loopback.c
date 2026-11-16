#include <stdint.h>
#include "gd32f10x.h"
#include "abox_uart.h"
#include "abox_platform_port.h"
#include "abox_board.h"

extern uint32_t _estack, _sbss, _ebss;
void Reset_Handler(void);
static void Default_Handler(void) { for (;;) {} }
void UART4_IRQHandler(void) { ABoxUart_Irq(ABOX_BOARD_UART_BUS); }
uint32_t SystemCoreClock = 8000000U;
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return 0; }

/* Values can be inspected by a debugger without changing Flash. */
volatile uint32_t g_uart_bench_ready, g_uart_bench_rx, g_uart_bench_tx, g_uart_bench_errors;
static uint8_t rx_buffer[16];
static const uint8_t request[4] = {0xA5U, 0x3CU, 0x96U, 0x69U};
static const uint8_t response[4] = {0x5AU, 0xC3U, 0x69U, 0x96U};

static void on_rx(void *context,const uint8_t *bytes, uint16_t length)
{
    (void)context;
    if (length != sizeof(request)) { ++g_uart_bench_errors; return; }
    for (unsigned i = 0U; i < sizeof(request); ++i)
        if (bytes[i] != request[i]) { ++g_uart_bench_errors; return; }
    ++g_uart_bench_rx;
    if (!ABoxUart_Send(ABOX_BOARD_UART_BUS, response, sizeof(response))) ++g_uart_bench_errors;
}
static void on_tx(void *context) { (void)context; ++g_uart_bench_tx; }
static void on_error(void *context) { (void)context; ++g_uart_bench_errors; }
static const ABoxUartEvents events = {0,on_rx,on_tx,on_error,0};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used))
const uintptr_t uart_bench_vectors[16U + 68U] = {
    [0] = (uintptr_t)&_estack, [1] = (uintptr_t)Reset_Handler,
    [2 ... 16U + 67U] = (uintptr_t)Default_Handler,
    [16U + UART4_IRQn] = (uintptr_t)UART4_IRQHandler
};
#pragma GCC diagnostic pop

void Reset_Handler(void)
{
    for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0U;
    const ABoxUartConfig config = {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC};
    if (!ABoxBoardUart_Prepare(ABOX_BOARD_UART_BUS) || !ABoxUart_Open(ABOX_BOARD_UART_BUS,&config) ||
        !ABoxUart_SetEvents(ABOX_BOARD_UART_BUS,&events) ||
        !ABoxUart_StartRx(ABOX_BOARD_UART_BUS,ABOX_UART_RX_IDLE,rx_buffer,sizeof(rx_buffer)))
        for (;;) {}
    g_uart_bench_ready = 1U;
    __enable_irq();
    for (;;) __WFI();
}
