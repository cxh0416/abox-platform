#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "gd32f10x.h"
#include "abox_serial_port.h"
#include "abox_gd32_serial.h"

uint32_t test_usart_status;
static uint32_t enabled, primask, tick, baud_configured;
static uint8_t received_byte, sent[16], rx_copy[16], *rearm_buffer;
static uint16_t sent_count, rx_count, rearm_size;
static unsigned rx_events, tx_events, error_events;

void rcu_periph_clock_enable(uint32_t p) { (void)p; }
void gpio_init(uint32_t p, uint32_t m, uint32_t s, uint32_t pins) { (void)p; (void)m; (void)s; (void)pins; }
void usart_deinit(uint32_t p) { (void)p; enabled = 0U; }
void usart_baudrate_set(uint32_t p, uint32_t baud) { (void)p; baud_configured = baud; }
void usart_word_length_set(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_stop_bit_set(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_parity_config(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_transmit_config(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_receive_config(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_enable(uint32_t p) { (void)p; }
void NVIC_SetPriority(int irq, uint32_t p) { assert(irq == UART4_IRQn && p == 0U); }
void NVIC_EnableIRQ(int irq) { assert(irq == UART4_IRQn); }
void usart_interrupt_enable(uint32_t p, uint32_t f) { (void)p; enabled |= 1U << f; }
void usart_interrupt_disable(uint32_t p, uint32_t f) { (void)p; enabled &= ~(1U << f); }
FlagStatus usart_interrupt_flag_get(uint32_t p, uint32_t f)
{ (void)p; return ((enabled & (1U << f)) && (test_usart_status & (f == USART_INT_TC ? USART_STAT_TC : USART_STAT_TBE))) ? SET : RESET; }
FlagStatus usart_flag_get(uint32_t p, uint32_t f) { (void)p; return (test_usart_status & f) ? SET : RESET; }
uint16_t usart_data_receive(uint32_t p)
{ (void)p; test_usart_status &= ~(USART_STAT_RBNE | USART_STAT_IDLEF | 15U); return received_byte; }
void usart_data_transmit(uint32_t p, uint16_t v)
{ (void)p; assert(sent_count < sizeof(sent)); sent[sent_count++] = (uint8_t)v; test_usart_status &= ~USART_STAT_TC; }
uint32_t test_get_primask(void) { return primask; }
void test_disable_irq(void) { primask = 1U; }
void test_set_primask(uint32_t k) { primask = k; }
uint32_t ABox_PortGetTickMs(void) { return tick++; }
static void on_rx(const uint8_t *p, uint16_t n)
{
    assert(n <= sizeof(rx_copy)); memcpy(rx_copy, p, n); rx_count = n; ++rx_events;
    if (rearm_buffer) assert(ABoxSerialPort_StartRx(rearm_buffer, rearm_size));
}
static void on_tx(void) { ++tx_events; }
static void on_error(void) { ++error_events; ABoxSerialPort_AbortTx(); }
static void incoming(uint32_t flags, uint8_t byte)
{ received_byte = byte; test_usart_status = flags; ABoxGdSerial_IrqHandler(); }

int main(void)
{
    uint8_t buffer[4], next[4];
    const uint8_t data[] = {1, 2, 3};
    const ABoxSerialPortEvents events = {on_rx, on_tx, on_error};
    ABoxSerialPort_SetEvents(&events);
    assert(ABoxSerialPort_Init() && baud_configured == 115200U);
    assert(!ABoxSerialPort_StartRx(0, 4U));
    assert(ABoxSerialPort_StartRx(buffer, sizeof(buffer)));
    assert(!ABoxSerialPort_StartRx(buffer, sizeof(buffer)));
    rearm_buffer = next; rearm_size = sizeof(next);
    incoming(USART_STAT_RBNE, 0x11U);
    incoming(USART_STAT_RBNE | USART_STAT_IDLEF, 0x22U);
    assert(rx_events == 1U && rx_count == 2U && rx_copy[0] == 0x11U && rx_copy[1] == 0x22U);
    incoming(USART_STAT_IDLEF, 0U);
    assert(rx_events == 1U);
    for (unsigned i = 0; i < 4; ++i) incoming(USART_STAT_RBNE | (i == 3 ? USART_STAT_IDLEF : 0U), (uint8_t)i);
    assert(rx_events == 2U && rx_count == 4U && rx_copy[3] == 3U);
    assert(ABoxSerialPort_Send(data, sizeof(data)));
    assert(!ABoxSerialPort_Send(data, sizeof(data)));
    for (unsigned i = 0; i < 3; ++i) { test_usart_status = USART_STAT_TBE; ABoxGdSerial_IrqHandler(); }
    assert(sent_count == 3U && !memcmp(sent, data, 3U) && tx_events == 0U);
    test_usart_status = USART_STAT_TC; ABoxGdSerial_IrqHandler();
    assert(tx_events == 1U);
    assert(ABoxSerialPort_Send(data, sizeof(data)));
    incoming(USART_STAT_ORERR | USART_STAT_RBNE, 0x55U);
    assert(error_events == 1U && tx_events == 1U);
    assert(ABoxSerialPort_StartRx(buffer, sizeof(buffer)));
    primask = 1U; ABoxSerialPort_AbortTx(); assert(primask == 1U);
    test_usart_status = 0U; tick = 0U;
    assert(!ABoxSerialPort_SendBlocking(data, sizeof(data), 3U));
    assert(ABoxSerialPort_Send(data, sizeof(data)));
    ABoxSerialPort_AbortTx();
    ABoxSerialPort_SetConfig(9600U); assert(!ABoxSerialPort_Init());
    return 0;
}
