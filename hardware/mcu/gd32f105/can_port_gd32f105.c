#include "abox_can_port.h"
#include "abox_can_filter.h"
#include "abox_can_board_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_can.h"
#include <string.h>
#ifdef ABOX_CAN_EVENTS_ENABLED
#include "abox_can_events.h"
#endif

static uint8_t ready[2];
static ABoxCanMode active_mode[2];
static uint32_t instance(unsigned bus) { return bus ? CAN1 : CAN0; }
int ABoxCanPort_IsReady(unsigned bus) { return bus < 2U && ready[bus]; }
static int filter_config(unsigned bus,unsigned slot,const ABoxCanFilter *rule,unsigned fifo)
{
    can_filter_parameter_struct filter;uint32_t id=0U,mask=0U;
    if (bus>1U || slot>=14U || fifo>1U || (rule && !ABoxCanFilter_Encode(rule,&id,&mask))) return 0;
    can_struct_para_init(CAN_FILTER_STRUCT,&filter);
    filter.filter_number=(uint16_t)((bus ? 14U : 0U)+slot);
    filter.filter_mode=CAN_FILTERMODE_MASK;filter.filter_bits=CAN_FILTERBITS_32BIT;
    filter.filter_fifo_number=fifo ? CAN_FIFO1 : CAN_FIFO0;filter.filter_enable=rule ? ENABLE : DISABLE;
    filter.filter_list_high=(uint16_t)(id>>16U);filter.filter_list_low=(uint16_t)id;
    filter.filter_mask_high=(uint16_t)(mask>>16U);filter.filter_mask_low=(uint16_t)mask;
    can_filter_init(&filter);return 1;
}
int ABoxCanPort_SetFilter(unsigned bus,unsigned slot,const ABoxCanFilter *rule,unsigned fifo)
{ return bus<2U && ready[bus] && filter_config(bus,slot,rule,fifo); }

int ABoxCanPort_Init(unsigned bus, const ABoxCanConfig *settings)
{
    can_parameter_struct config;
    can_filter_parameter_struct filter;
    uint32_t clock, divisor, prescaler, quanta, filter_id, filter_mask;
    if (!settings || settings->options.auto_retransmit > 1U ||
        settings->options.auto_bus_off_recovery > 1U || settings->options.rx_fifo_overwrite > 1U ||
        settings->segment1 < 1U || settings->segment1 > 16U ||
        settings->segment2 < 1U || settings->segment2 > 8U ||
        settings->sync_jump_width < 1U || settings->sync_jump_width > 4U ||
        settings->sync_jump_width > settings->segment2 || bus > 1U || !settings->bitrate ||
        (unsigned)settings->mode > ABOX_CAN_SILENT_LOOPBACK ||
        !ABoxCanFilter_Encode(&settings->filter, &filter_id, &filter_mask) || ready[bus] ||
        (!bus && ready[1])) return 0;
    quanta = 1U + settings->segment1 + settings->segment2;
    if (settings->bitrate > UINT32_MAX / quanta) return 0;
    clock = rcu_clock_freq_get(CK_APB1); divisor = settings->bitrate * quanta;
    if (!clock || clock % divisor || !(prescaler = clock / divisor) || prescaler > 1024U) return 0;
    rcu_periph_clock_enable(RCU_CAN0);
    if (bus) rcu_periph_clock_enable(RCU_CAN1);
    if (!ABoxCanBoard_Prepare(bus,settings->mode)) return 0;
    can_deinit(instance(bus));
    can_struct_para_init(CAN_INIT_STRUCT, &config);
    config.working_mode = settings->mode == ABOX_CAN_NORMAL ? CAN_NORMAL_MODE :
                          settings->mode == ABOX_CAN_LOOPBACK ? CAN_LOOPBACK_MODE :
                          settings->mode == ABOX_CAN_SILENT ? CAN_SILENT_MODE : CAN_SILENT_LOOPBACK_MODE;
    config.resync_jump_width = (uint8_t)(settings->sync_jump_width - 1U);
    config.time_segment_1 = (uint8_t)(settings->segment1 - 1U);
    config.time_segment_2 = (uint8_t)(settings->segment2 - 1U);
    config.prescaler = (uint16_t)prescaler;
    config.auto_retrans = settings->options.auto_retransmit ? ENABLE : DISABLE;
    config.auto_bus_off_recovery = settings->options.auto_bus_off_recovery ? ENABLE : DISABLE;
    config.rec_fifo_overwrite = settings->options.rx_fifo_overwrite ? ENABLE : DISABLE;
    config.auto_wake_up = ENABLE;
    if (can_init(instance(bus), &config) != SUCCESS) {
        can_deinit(instance(bus));
        ABoxCanBoard_Release(bus,settings->mode);
        return 0;
    }
    can1_filter_start_bank(14U);
    for (unsigned slot=1U;slot<14U;++slot) (void)filter_config(bus,slot,0,0U);
    can_struct_para_init(CAN_FILTER_STRUCT, &filter);
    filter.filter_number = (uint16_t)(bus ? 14U : 0U);
    filter.filter_mode = CAN_FILTERMODE_MASK; filter.filter_bits = CAN_FILTERBITS_32BIT;
    filter.filter_fifo_number = CAN_FIFO0; filter.filter_enable = ENABLE;
    filter.filter_list_high = (uint16_t)(filter_id >> 16U); filter.filter_list_low = (uint16_t)filter_id;
    filter.filter_mask_high = (uint16_t)(filter_mask >> 16U); filter.filter_mask_low = (uint16_t)filter_mask;
    can_filter_init(&filter);
    active_mode[bus] = settings->mode;
    ready[bus] = 1U;
    return 1;
}

int ABoxCanPort_Stop(unsigned bus)
{
    if (bus > 1U || !ready[bus] || (!bus && ready[1])) return 0;
#ifdef ABOX_CAN_EVENTS_ENABLED
    ABoxCanPort_DisableEventIrq(bus);
#endif
    ABoxCanBoard_Release(bus,active_mode[bus]);
    can_deinit(instance(bus)); ready[bus] = 0U; return 1;
}

int ABoxCanPort_Suspend(unsigned bus)
{
    /* Board release must be nonblocking when called from a critical section. */
    return ABoxCanPort_Stop(bus);
}

int ABoxCanPort_Send(unsigned bus, const ABoxCanFrame *frame)
{
    uint8_t mailbox;
    return ABoxCanPort_SendTracked(bus, frame, &mailbox);
}

int ABoxCanPort_SendTracked(unsigned bus, const ABoxCanFrame *frame, uint8_t *selected_mailbox)
{
    can_trasnmit_message_struct message;
    uint8_t mailbox;
    if (bus > 1U || !ready[bus] || !frame || !selected_mailbox || frame->length > 8U ||
        frame->extended > 1U || frame->remote > 1U ||
        frame->id > (frame->extended ? 0x1FFFFFFFU : 0x7FFU)) return -1;
    can_struct_para_init(CAN_TX_MESSAGE_STRUCT, &message);
    message.tx_ff = (uint8_t)(frame->extended ? CAN_FF_EXTENDED : CAN_FF_STANDARD);
    message.tx_ft = (uint8_t)(frame->remote ? CAN_FT_REMOTE : CAN_FT_DATA);
    if (frame->extended) message.tx_efid = frame->id; else message.tx_sfid = frame->id;
    message.tx_dlen = frame->length;
    if (!frame->remote) memcpy(message.tx_data, frame->data, frame->length);
    mailbox = can_message_transmit(instance(bus), &message);
    if (mailbox == CAN_NOMAILBOX) return 0;
    if (mailbox > 2U) return -1;
    *selected_mailbox = mailbox;
    return 1;
}

int ABoxCanPort_ReceiveFifo(unsigned bus,unsigned fifo,ABoxCanFrame *frame)
{
    can_receive_message_struct message;
    uint8_t selected=(uint8_t)(fifo ? CAN_FIFO1 : CAN_FIFO0);
    if (bus > 1U || fifo>1U || !ready[bus] || !frame) return -1;
    if (!can_receive_message_length_get(instance(bus), selected)) return 0;
    can_struct_para_init(CAN_RX_MESSAGE_STRUCT, &message);
    can_message_receive(instance(bus), selected, &message);
    if (message.rx_dlen > 8U || (message.rx_ff != CAN_FF_STANDARD && message.rx_ff != CAN_FF_EXTENDED) ||
        (message.rx_ft != CAN_FT_DATA && message.rx_ft != CAN_FT_REMOTE)) return -1;
    memset(frame, 0, sizeof(*frame));
    frame->extended = message.rx_ff == CAN_FF_EXTENDED; frame->remote = message.rx_ft == CAN_FT_REMOTE;
    frame->id = frame->extended ? message.rx_efid : message.rx_sfid; frame->length = message.rx_dlen;
    if (!frame->remote) memcpy(frame->data, message.rx_data, frame->length);
    return 1;
}
int ABoxCanPort_Receive(unsigned bus,ABoxCanFrame *frame)
{ return ABoxCanPort_ReceiveFifo(bus,0U,frame); }
