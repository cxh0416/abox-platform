#include "abox_can_port.h"
#include "gd32f10x.h"
#include "gd32f10x_can.h"
#include <string.h>

static uint8_t ready[2];
static uint32_t instance(unsigned bus) { return bus ? CAN1 : CAN0; }

int ABoxCanPort_Init(unsigned bus, uint32_t bitrate, ABoxCanMode mode)
{
    can_parameter_struct config;
    can_filter_parameter_struct filter;
    uint32_t clock, divisor, prescaler;
    if (bus > 1U || !bitrate || bitrate > UINT32_MAX / 18U ||
        (mode != ABOX_CAN_NORMAL && mode != ABOX_CAN_SILENT_LOOPBACK) || ready[bus] ||
        (!bus && ready[1])) return 0;
    clock = rcu_clock_freq_get(CK_APB1); divisor = bitrate * 18U;
    if (!clock || clock % divisor || !(prescaler = clock / divisor) || prescaler > 1024U) return 0;
    rcu_periph_clock_enable(RCU_CAN0);
    if (bus) rcu_periph_clock_enable(RCU_CAN1);
    if (mode == ABOX_CAN_NORMAL) {
        uint32_t port = bus ? GPIOB : GPIOA;
        uint32_t standby = 1U << (bus ? 14U : 15U);
        rcu_periph_clock_enable(bus ? RCU_GPIOB : RCU_GPIOA);
        /* Existing board contract drives CANx_STB low for normal operation. */
        rcu_periph_clock_enable(RCU_GPIOB);
        gpio_bit_reset(GPIOB, standby);
        gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, standby);
        rcu_periph_clock_enable(RCU_AF);
        gpio_pin_remap_config(bus ? GPIO_CAN1_REMAP : GPIO_CAN0_FULL_REMAP, DISABLE);
        gpio_init(port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, 1U << (bus ? 12 : 11));
        gpio_init(port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, 1U << (bus ? 13 : 12));
    }
    can_deinit(instance(bus));
    can_struct_para_init(CAN_INIT_STRUCT, &config);
    config.working_mode = mode == ABOX_CAN_NORMAL ? CAN_NORMAL_MODE : CAN_SILENT_LOOPBACK_MODE;
    config.resync_jump_width = CAN_BT_SJW_1TQ;
    config.time_segment_1 = CAN_BT_BS1_15TQ; config.time_segment_2 = CAN_BT_BS2_2TQ;
    config.prescaler = (uint16_t)prescaler;
    config.auto_retrans = ENABLE; config.auto_wake_up = ENABLE;
    if (can_init(instance(bus), &config) != SUCCESS) { can_deinit(instance(bus)); return 0; }
    can1_filter_start_bank(14U);
    can_struct_para_init(CAN_FILTER_STRUCT, &filter);
    filter.filter_number = (uint16_t)(bus ? 14U : 0U);
    filter.filter_mode = CAN_FILTERMODE_MASK; filter.filter_bits = CAN_FILTERBITS_32BIT;
    filter.filter_fifo_number = CAN_FIFO0; filter.filter_enable = ENABLE;
    can_filter_init(&filter);
    ready[bus] = 1U;
    return 1;
}

int ABoxCanPort_Stop(unsigned bus)
{
    if (bus > 1U || !ready[bus] || (!bus && ready[1])) return 0;
    can_deinit(instance(bus)); ready[bus] = 0U; return 1;
}

int ABoxCanPort_Send(unsigned bus, const ABoxCanFrame *frame)
{
    can_trasnmit_message_struct message;
    uint8_t mailbox;
    if (bus > 1U || !ready[bus] || !frame || frame->length > 8U ||
        frame->extended > 1U || frame->remote > 1U ||
        frame->id > (frame->extended ? 0x1FFFFFFFU : 0x7FFU)) return -1;
    can_struct_para_init(CAN_TX_MESSAGE_STRUCT, &message);
    message.tx_ff = (uint8_t)(frame->extended ? CAN_FF_EXTENDED : CAN_FF_STANDARD);
    message.tx_ft = (uint8_t)(frame->remote ? CAN_FT_REMOTE : CAN_FT_DATA);
    if (frame->extended) message.tx_efid = frame->id; else message.tx_sfid = frame->id;
    message.tx_dlen = frame->length;
    if (!frame->remote) memcpy(message.tx_data, frame->data, frame->length);
    mailbox = can_message_transmit(instance(bus), &message);
    return mailbox == CAN_NOMAILBOX ? 0 : mailbox < CAN_NOMAILBOX ? 1 : -1;
}

int ABoxCanPort_Receive(unsigned bus, ABoxCanFrame *frame)
{
    can_receive_message_struct message;
    if (bus > 1U || !ready[bus] || !frame) return -1;
    if (!can_receive_message_length_get(instance(bus), CAN_FIFO0)) return 0;
    can_struct_para_init(CAN_RX_MESSAGE_STRUCT, &message);
    can_message_receive(instance(bus), CAN_FIFO0, &message);
    if (message.rx_dlen > 8U || (message.rx_ff != CAN_FF_STANDARD && message.rx_ff != CAN_FF_EXTENDED) ||
        (message.rx_ft != CAN_FT_DATA && message.rx_ft != CAN_FT_REMOTE)) return -1;
    memset(frame, 0, sizeof(*frame));
    frame->extended = message.rx_ff == CAN_FF_EXTENDED; frame->remote = message.rx_ft == CAN_FT_REMOTE;
    frame->id = frame->extended ? message.rx_efid : message.rx_sfid; frame->length = message.rx_dlen;
    if (!frame->remote) memcpy(frame->data, message.rx_data, frame->length);
    return 1;
}
