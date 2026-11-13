#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "gd32f10x.h"
#include "abox_gd32_modem.h"
#include "abox_modem_port.h"
#include "abox_ec800_rx.h"

#define CAPACITY 1088U
uint32_t test_usart_status, test_usart_data;
static uint32_t primask, pending, remaining, dma_enabled, tick, inject_boundary;
static uint8_t dma_buffer[CAPACITY], ring[1152], output[CAPACITY + 32];
static unsigned output_size, errors, positions;
static ABoxEc800Rx rx;

void rcu_periph_clock_enable(uint32_t p) { (void)p; }
void gpio_init(uint32_t p, uint32_t m, uint32_t s, uint32_t pins) { (void)p; (void)m; (void)s; (void)pins; }
void usart_deinit(uint32_t p) { (void)p; }
void usart_baudrate_set(uint32_t p, uint32_t b) { (void)p; assert(b == 115200U); }
void usart_word_length_set(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_stop_bit_set(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_parity_config(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_transmit_config(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_receive_config(uint32_t p, uint32_t v) { (void)p; (void)v; }
void usart_enable(uint32_t p) { (void)p; }
void NVIC_SetPriority(int irq, uint32_t v) { assert((irq == USART0_IRQn || irq == DMA0_Channel4_IRQn) && v == 0U); }
void NVIC_EnableIRQ(int irq) { assert(irq == USART0_IRQn || irq == DMA0_Channel4_IRQn); }
void usart_interrupt_enable(uint32_t p, uint32_t f) { (void)p; (void)f; }
void usart_interrupt_disable(uint32_t p, uint32_t f) { (void)p; (void)f; }
void usart_dma_receive_config(uint32_t p, uint8_t v) { (void)p; dma_enabled = v; }
FlagStatus usart_flag_get(uint32_t p, uint32_t f) { (void)p; return (test_usart_status & f) ? SET : RESET; }
uint16_t usart_data_receive(uint32_t p) { (void)p; test_usart_status = 0U; return 0U; }
void usart_data_transmit(uint32_t p, uint16_t v) { (void)p; (void)v; }
uint32_t test_get_primask(void) { return primask; }
void test_disable_irq(void) { primask = 1U; }
void test_set_primask(uint32_t key) { primask = key; }
uint32_t ABox_PortGetTickMs(void) { return tick++; }
void dma_deinit(uint32_t d, uint32_t c) { (void)d; (void)c; }
void dma_struct_para_init(dma_parameter_struct *config) { memset(config, 0, sizeof(*config)); }
void dma_init(uint32_t d, uint32_t c, dma_parameter_struct *config)
{ (void)d; (void)c; assert(config->number == CAPACITY && config->memory_inc == DMA_MEMORY_INCREASE_ENABLE); remaining = config->number; }
void dma_circulation_enable(uint32_t d, uint32_t c) { (void)d; (void)c; }
void dma_channel_enable(uint32_t d, uint32_t c) { (void)d; (void)c; }
void dma_channel_disable(uint32_t d, uint32_t c) { (void)d; (void)c; }
void dma_flag_clear(uint32_t d, uint32_t c, uint32_t f) { (void)d; (void)c; (void)f; pending = 0U; }
void dma_interrupt_enable(uint32_t d, uint32_t c, uint32_t f) { (void)d; (void)c; (void)f; }
void dma_interrupt_disable(uint32_t d, uint32_t c, uint32_t f) { (void)d; (void)c; (void)f; }
FlagStatus dma_interrupt_flag_get(uint32_t d, uint32_t c, uint32_t f)
{ (void)d; (void)c; return (pending & f) ? SET : RESET; }
void dma_interrupt_flag_clear(uint32_t d, uint32_t c, uint32_t f) { (void)d; (void)c; pending &= ~f; }
uint32_t dma_transfer_number_get(uint32_t d, uint32_t c)
{
    (void)d; (void)c;
    if (inject_boundary) { pending |= DMA_INT_FLAG_HTF; remaining = CAPACITY / 2U; inject_boundary = 0U; }
    return remaining;
}
static void on_rx(uint16_t position) { ++positions; assert(ABoxEc800Rx_OnDmaEvent(&rx, position)); }
static void on_error(void) { ++errors; ABoxEc800Rx_RequestRestart(&rx); }
static void sink(void *context, const uint8_t *data, uint16_t count)
{ (void)context; assert(output_size + count <= sizeof(output)); memcpy(output + output_size, data, count); output_size += count; }
static void idle(uint32_t left) { remaining = left; test_usart_status = USART_STAT_IDLEF; ABoxGdModem_UsartIrq(); }
static void reset_rx(void)
{
    assert(ABoxModemPort_AbortRx()); ABoxEc800Rx_AfterRestart(&rx);
    assert(ABoxModemPort_StartRx(dma_buffer, CAPACITY));
}
int main(void)
{
    const ABoxModemPortEvents events = {on_rx, on_error};
    assert(ABoxEc800Rx_Init(&rx, dma_buffer, CAPACITY, ring, sizeof(ring)));
    ABoxModemPort_SetEvents(&events);
    assert(!ABoxModemPort_StartRx(dma_buffer, CAPACITY));
    assert(ABoxGdModem_Init());
    assert(ABoxModemPort_StartRx(dma_buffer, CAPACITY) && dma_enabled);
    assert(!ABoxModemPort_StartRx(dma_buffer, CAPACITY));
    for (unsigned i = 0; i < CAPACITY; ++i) dma_buffer[i] = (uint8_t)i;
    idle(CAPACITY - 200U);
    pending = DMA_INT_FLAG_HTF; remaining = CAPACITY / 2U; ABoxGdModem_DmaIrq();
    pending = DMA_INT_FLAG_FTF; remaining = CAPACITY; ABoxGdModem_DmaIrq();
    for (unsigned i = 0; i < 12; ++i) dma_buffer[i] = (uint8_t)(CAPACITY + i);
    idle(CAPACITY - 12U);
    assert(ABoxEc800Rx_Drain(&rx, sink, 0, 0) == CAPACITY + 12U);
    for (unsigned i = 0; i < CAPACITY + 12U; ++i) assert(output[i] == (uint8_t)i);
    assert(positions == 4U && !errors);
    reset_rx(); inject_boundary = 1U; idle(CAPACITY - 500U);
    assert(positions == 5U && !errors); /* boundary during idle counter read */
    reset_rx(); pending = DMA_INT_FLAG_FTF; ABoxGdModem_DmaIrq();
    assert(errors == 1U && !dma_enabled && rx.restart_requested);
    reset_rx(); pending = DMA_INT_FLAG_ERR; ABoxGdModem_DmaIrq();
    assert(errors == 2U && !dma_enabled);
    reset_rx(); test_usart_status = USART_STAT_ORERR; ABoxGdModem_UsartIrq();
    assert(errors == 3U && !dma_enabled);
    primask = 1U; assert(ABoxModemPort_AbortRx() && primask == 1U);
    return 0;
}
