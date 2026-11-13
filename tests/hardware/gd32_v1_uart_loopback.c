#include <stdint.h>
#include "gd32f10x.h"
#include "abox_serial_port.h"
#include "abox_gd32_serial.h"

extern uint32_t _estack, _sbss, _ebss;
void Reset_Handler(void);
static void Default_Handler(void) { for (;;) {} }
void UART4_IRQHandler(void) { ABoxGdSerial_IrqHandler(); }
uint32_t SystemCoreClock = 8000000U;
uint32_t ABox_PortGetTickMs(void) { return 0U; }

/* Values can be inspected by a debugger without changing Flash. */
volatile uint32_t g_uart_bench_ready, g_uart_bench_rx, g_uart_bench_tx, g_uart_bench_errors;
static uint8_t rx_buffer[16];
static const uint8_t request[4] = {0xA5U, 0x3CU, 0x96U, 0x69U};
static const uint8_t response[4] = {0x5AU, 0xC3U, 0x69U, 0x96U};

static void on_rx(const uint8_t *bytes, uint16_t length)
{
    if (length != sizeof(request)) { ++g_uart_bench_errors; return; }
    for (unsigned i = 0U; i < sizeof(request); ++i)
        if (bytes[i] != request[i]) { ++g_uart_bench_errors; return; }
    ++g_uart_bench_rx;
    if (!ABoxSerialPort_Send(response, sizeof(response))) ++g_uart_bench_errors;
}
static void on_tx(void) { ++g_uart_bench_tx; }
static void on_error(void) { ++g_uart_bench_errors; }

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
    static const ABoxSerialPortEvents events = {on_rx, on_tx, on_error};
    for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0U;
    ABoxSerialPort_SetEvents(&events);
    if (!ABoxSerialPort_Init() || !ABoxSerialPort_StartRx(rx_buffer, sizeof(rx_buffer)))
        for (;;) {}
    g_uart_bench_ready = 1U;
    __enable_irq();
    for (;;) __WFI();
}
