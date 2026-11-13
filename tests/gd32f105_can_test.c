#include <assert.h>
#include <string.h>
#include "gd32f10x.h"
#include "gd32f10x_can.h"
#include "abox_can_port.h"

static uint32_t clock_hz = 36000000U, last_bus, init_count, gpio_calls, remap, queued, standby_low;
static uint8_t mailbox, fifo_count;
static can_parameter_struct last_config;
static can_filter_parameter_struct last_filter;
static can_trasnmit_message_struct sent;
static can_receive_message_struct received;
uint32_t rcu_clock_freq_get(uint32_t clock) { assert(clock == CK_APB1); return clock_hz; }
void rcu_periph_clock_enable(uint32_t p) { (void)p; }
void gpio_init(uint32_t p, uint32_t m, uint32_t s, uint32_t pins)
{ (void)s; if (m == GPIO_MODE_OUT_PP) assert(p == GPIOB && (standby_low & pins)); ++gpio_calls; }
void gpio_bit_reset(uint32_t p, uint32_t pins) { assert(p == GPIOB); standby_low |= pins; }
void gpio_pin_remap_config(uint32_t r, uint32_t enabled) { assert(enabled == DISABLE); remap = r; }
void can_deinit(uint32_t p) { last_bus = p; }
void can_struct_para_init(uint32_t type, void *p)
{
    if (type == CAN_INIT_STRUCT) memset(p, 0, sizeof(can_parameter_struct));
    else if (type == CAN_FILTER_STRUCT) memset(p, 0, sizeof(can_filter_parameter_struct));
    else if (type == CAN_TX_MESSAGE_STRUCT) memset(p, 0, sizeof(can_trasnmit_message_struct));
    else { assert(type == CAN_RX_MESSAGE_STRUCT); memset(p, 0, sizeof(can_receive_message_struct)); }
}
uint32_t can_init(uint32_t p, can_parameter_struct *c) { last_bus = p; last_config = *c; ++init_count; return SUCCESS; }
void can1_filter_start_bank(uint8_t bank) { assert(bank == 14U); }
void can_filter_init(can_filter_parameter_struct *f) { last_filter = *f; }
uint8_t can_message_transmit(uint32_t p, can_trasnmit_message_struct *m)
{ last_bus = p; sent = *m; ++queued; return mailbox; }
uint8_t can_receive_message_length_get(uint32_t p, uint8_t fifo)
{ last_bus = p; assert(fifo == CAN_FIFO0); return fifo_count; }
void can_message_receive(uint32_t p, uint8_t fifo, can_receive_message_struct *m)
{ last_bus = p; assert(fifo == CAN_FIFO0); *m = received; fifo_count = 0; }

int main(void)
{
    ABoxCanFrame frame = {0}, output = {0};
    assert(!ABoxCanPort_Init(2U, 250000U, ABOX_CAN_NORMAL));
    assert(!ABoxCanPort_Init(0U, 0U, ABOX_CAN_NORMAL));
    assert(!ABoxCanPort_Init(0U, 123456U, ABOX_CAN_NORMAL));
    assert(!init_count && !gpio_calls);
    assert(ABoxCanPort_Init(0U, 250000U, ABOX_CAN_SILENT_LOOPBACK));
    assert(last_bus == CAN0 && last_config.prescaler == 8U && last_config.working_mode == CAN_SILENT_LOOPBACK_MODE);
    assert(last_filter.filter_number == 0U && last_filter.filter_enable == ENABLE && !gpio_calls);
    assert(!ABoxCanPort_Init(0U, 250000U, ABOX_CAN_NORMAL));
    assert(ABoxCanPort_Init(1U, 500000U, ABOX_CAN_NORMAL));
    assert(last_bus == CAN1 && last_config.prescaler == 4U && last_filter.filter_number == 14U);
    assert(gpio_calls == 3U && remap == GPIO_CAN1_REMAP && standby_low == (1U << 14));
    frame.id = 0x800U; frame.length = 8U; assert(ABoxCanPort_Send(0U, &frame) == -1 && !queued);
    frame.id = 0x1234567U; frame.extended = 1U; frame.length = 9U;
    assert(ABoxCanPort_Send(0U, &frame) == -1 && !queued);
    frame.length = 2U; frame.data[0] = 0x34U; frame.data[1] = 0x56U;
    mailbox = CAN_NOMAILBOX; assert(ABoxCanPort_Send(1U, &frame) == 0);
    mailbox = 2U; assert(ABoxCanPort_Send(1U, &frame) == 1);
    assert(last_bus == CAN1 && sent.tx_ff == CAN_FF_EXTENDED && sent.tx_efid == frame.id &&
           sent.tx_dlen == 2U && sent.tx_data[0] == 0x34U && sent.tx_data[1] == 0x56U);
    assert(ABoxCanPort_Receive(1U, &output) == 0);
    fifo_count = 1; received.rx_ff = CAN_FF_EXTENDED; received.rx_ft = CAN_FT_REMOTE;
    received.rx_efid = frame.id; received.rx_dlen = 2U; memset(received.rx_data, 0xAA, 8U);
    assert(ABoxCanPort_Receive(1U, &output) == 1 && output.id == frame.id && output.remote &&
           output.extended && output.length == 2U && output.data[0] == 0U);
    fifo_count = 1; received.rx_dlen = 9U; assert(ABoxCanPort_Receive(1U, &output) == -1);
    assert(!ABoxCanPort_Stop(0U) && ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
    assert(ABoxCanPort_Send(0U, &frame) == -1 && ABoxCanPort_Receive(0U, &output) == -1);
    standby_low = 0U; gpio_calls = 0U;
    assert(ABoxCanPort_Init(0U, 250000U, ABOX_CAN_NORMAL));
    assert(standby_low == (1U << 15) && gpio_calls == 3U);
    assert(ABoxCanPort_Stop(0U));
    return 0;
}
