#include <assert.h>
#include <string.h>
#include "gd32f10x.h"
#include "gd32f10x_can.h"
#include "abox_can_port.h"
int ABoxGdV1Can_Bind(void);

static uint32_t clock_hz = 36000000U, last_bus, init_count, gpio_calls, remap, queued, standby_low;
static uint8_t mailbox, fifo_count;
static unsigned can_failure, board_failure, board_prepares, board_releases;
static can_parameter_struct last_config;
static can_filter_parameter_struct last_filter;
static can_filter_parameter_struct filter_banks[28];static unsigned selected_fifo;
static can_trasnmit_message_struct sent;
static can_receive_message_struct received;
uint32_t rcu_clock_freq_get(uint32_t clock) { assert(clock == CK_APB1); return clock_hz; }
void rcu_periph_clock_enable(uint32_t p) { (void)p; }
void gpio_init(uint32_t p, uint32_t m, uint32_t s, uint32_t pins)
{ (void)s; if (m == GPIO_MODE_OUT_PP) assert(p == GPIOB && (standby_low & pins)); ++gpio_calls; }
void gpio_bit_reset(uint32_t p, uint32_t pins) { assert(p == GPIOB); standby_low |= pins; }
void gpio_bit_set(uint32_t p, uint32_t pins) { assert(p == GPIOB); standby_low &= ~pins; }
void gpio_pin_remap_config(uint32_t r, uint32_t enabled) { assert(enabled == DISABLE); remap = r; }
void can_deinit(uint32_t p) { last_bus = p; }
void can_struct_para_init(uint32_t type, void *p)
{
    if (type == CAN_INIT_STRUCT) memset(p, 0, sizeof(can_parameter_struct));
    else if (type == CAN_FILTER_STRUCT) memset(p, 0, sizeof(can_filter_parameter_struct));
    else if (type == CAN_TX_MESSAGE_STRUCT) memset(p, 0, sizeof(can_trasnmit_message_struct));
    else { assert(type == CAN_RX_MESSAGE_STRUCT); memset(p, 0, sizeof(can_receive_message_struct)); }
}
uint32_t can_init(uint32_t p, can_parameter_struct *c) { last_bus = p; last_config = *c; ++init_count; return can_failure ? 0U : SUCCESS; }
void can1_filter_start_bank(uint8_t bank) { assert(bank == 14U); }
void can_filter_init(can_filter_parameter_struct *f) { last_filter = *f; filter_banks[f->filter_number]=*f; }
uint8_t can_message_transmit(uint32_t p, can_trasnmit_message_struct *m)
{ last_bus = p; sent = *m; ++queued; return mailbox; }
uint8_t can_receive_message_length_get(uint32_t p, uint8_t fifo)
{ last_bus = p; selected_fifo=fifo; return fifo_count; }
void can_message_receive(uint32_t p, uint8_t fifo, can_receive_message_struct *m)
{ last_bus = p; selected_fifo=fifo; *m = received; fifo_count = 0; }
static int custom_prepare(void *context, unsigned bus, ABoxCanMode mode)
{ assert(context == &board_prepares && bus == 0U && mode == ABOX_CAN_NORMAL); ++board_prepares; return !board_failure; }
static void custom_release(void *context, unsigned bus, ABoxCanMode mode)
{ assert(context == &board_prepares && bus == 0U && mode == ABOX_CAN_NORMAL); ++board_releases; }

int main(void)
{
    ABoxCanFrame frame = {0}, output = {0};
    uint8_t selected = 99U;
    ABoxCanConfig config = {250000U, 15U, 2U, 1U, ABOX_CAN_SILENT_LOOPBACK, {1U, 0U, 0U}, {0}};
    assert(!ABoxCanPort_Init(0U, NULL));
    config.options.auto_retransmit = 2U; assert(!ABoxCanPort_Init(0U, &config));
    config.options.auto_retransmit = 1U;
    assert(!ABoxCanPort_Init(2U, &config));
    config.bitrate = 0U; assert(!ABoxCanPort_Init(0U, &config));
    config.bitrate = 123456U; assert(!ABoxCanPort_Init(0U, &config));
    assert(!init_count && !gpio_calls);
    config.bitrate=250000U;config.mode=ABOX_CAN_NORMAL;
    assert(ABoxCanPort_Init(0U,&config) && !gpio_calls && !standby_low);
    assert(!ABoxGdV1Can_Bind());
    assert(ABoxCanPort_Stop(0U) && ABoxGdV1Can_Bind());
    config.mode=ABOX_CAN_SILENT_LOOPBACK;
    config.bitrate = 250000U;
    assert(ABoxCanPort_Init(0U, &config));
    assert(last_bus == CAN0 && last_config.prescaler == 8U && last_config.working_mode == CAN_SILENT_LOOPBACK_MODE);
    assert(last_config.auto_retrans == ENABLE && last_config.auto_bus_off_recovery == DISABLE && last_config.rec_fifo_overwrite == DISABLE);
    assert(last_filter.filter_number == 0U && last_filter.filter_enable == ENABLE && !gpio_calls);
    assert(!ABoxCanPort_Init(0U, &config));
    config.bitrate = 500000U; config.mode = ABOX_CAN_NORMAL;
    assert(ABoxCanPort_Init(1U, &config));
    assert(last_bus == CAN1 && last_config.prescaler == 4U && last_filter.filter_number == 14U);
    {
        ABoxCanFilter rule={0x321U,0x7ffU,1U,0U,1U,0U};
        assert(!ABoxCanPort_SetFilter(2U,1U,&rule,0U));
        assert(!ABoxCanPort_SetFilter(1U,14U,&rule,0U));
        assert(!ABoxCanPort_SetFilter(1U,1U,&rule,2U));
        assert(ABoxCanPort_SetFilter(1U,3U,&rule,1U));
        assert(filter_banks[17].filter_enable==ENABLE && filter_banks[17].filter_fifo_number==CAN_FIFO1);
        assert(filter_banks[0].filter_enable==ENABLE && filter_banks[14].filter_enable==ENABLE);
        assert(ABoxCanPort_SetFilter(1U,3U,NULL,1U) && filter_banks[17].filter_enable==DISABLE);
        assert(ABoxCanPort_SetFilter(1U,3U,&rule,1U));
    }
    assert(gpio_calls == 3U && remap == GPIO_CAN1_REMAP && standby_low == (1U << 14));
    frame.id = 0x800U; frame.length = 8U; assert(ABoxCanPort_Send(0U, &frame) == -1 && !queued);
    frame.id = 0x1234567U; frame.extended = 1U; frame.length = 9U;
    assert(ABoxCanPort_Send(0U, &frame) == -1 && !queued);
    frame.length = 2U; frame.data[0] = 0x34U; frame.data[1] = 0x56U;
    mailbox = CAN_NOMAILBOX; assert(ABoxCanPort_Send(1U, &frame) == 0);
    assert(ABoxCanPort_SendTracked(1U, &frame, &selected) == 0 && selected == 99U);
    mailbox = 2U; assert(ABoxCanPort_Send(1U, &frame) == 1);
    assert(ABoxCanPort_SendTracked(1U, &frame, &selected) == 1 && selected == 2U);
    assert(ABoxCanPort_SendTracked(1U, &frame, NULL) == -1);
    assert(last_bus == CAN1 && sent.tx_ff == CAN_FF_EXTENDED && sent.tx_efid == frame.id &&
           sent.tx_dlen == 2U && sent.tx_data[0] == 0x34U && sent.tx_data[1] == 0x56U);
    assert(ABoxCanPort_Receive(1U, &output) == 0);
    fifo_count = 1; received.rx_ff = CAN_FF_EXTENDED; received.rx_ft = CAN_FT_REMOTE;
    received.rx_efid = frame.id; received.rx_dlen = 2U; memset(received.rx_data, 0xAA, 8U);
    assert(ABoxCanPort_Receive(1U, &output) == 1 && output.id == frame.id && output.remote &&
           output.extended && output.length == 2U && output.data[0] == 0U);
    fifo_count = 1; received.rx_dlen = 9U; assert(ABoxCanPort_Receive(1U, &output) == -1);
    received.rx_dlen=2U;fifo_count=1U;assert(ABoxCanPort_ReceiveFifo(1U,1U,&output)==1 && selected_fifo==CAN_FIFO1);
    assert(ABoxCanPort_ReceiveFifo(1U,2U,&output)==-1);
    assert(!ABoxCanPort_Stop(0U) && ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
    assert(ABoxCanPort_Init(0U,&config) && ABoxCanPort_Init(1U,&config));
    assert(filter_banks[17].filter_enable==DISABLE);
    assert(ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
    assert(ABoxCanPort_Send(0U, &frame) == -1 && ABoxCanPort_Receive(0U, &output) == -1);
    standby_low = 0U; gpio_calls = 0U;
    config.bitrate = 250000U;
    assert(ABoxCanPort_Init(0U, &config));
    assert(standby_low == (1U << 15) && gpio_calls == 3U);
    assert(ABoxCanPort_Stop(0U));
    config = (ABoxCanConfig){500000U, 15U, 2U, 1U, ABOX_CAN_SILENT_LOOPBACK, {0U, 1U, 1U}, {0}};
    assert(ABoxCanPort_Init(0U, &config));
    assert(last_config.auto_retrans == DISABLE && last_config.auto_bus_off_recovery == ENABLE && last_config.rec_fifo_overwrite == ENABLE);
    assert(ABoxCanPort_Stop(0U));
    config = (ABoxCanConfig){250000U, 13U, 2U, 2U, ABOX_CAN_SILENT_LOOPBACK, {1U, 0U, 1U}, {0}};
    assert(ABoxCanPort_Init(0U, &config));
    assert(last_config.prescaler == 9U && last_config.time_segment_1 == 12U &&
           last_config.time_segment_2 == 1U && last_config.resync_jump_width == 1U);
    assert(ABoxCanPort_Stop(0U));
    config.sync_jump_width = 3U; assert(!ABoxCanPort_Init(0U, &config));
    config.sync_jump_width = 1U; config.segment1 = 0U; assert(!ABoxCanPort_Init(0U, &config));
    config.segment1 = 15U; config.mode = ABOX_CAN_SILENT;
    config.filter = (ABoxCanFilter){0x321U, 0x7F0U, 1U, 0U, 1U, 0U};
    assert(ABoxCanPort_Init(0U, &config));
    assert(last_config.working_mode == CAN_SILENT_MODE &&
           (((uint32_t)last_filter.filter_list_high << 16U) | last_filter.filter_list_low) == (0x321U << 21U) &&
           (((uint32_t)last_filter.filter_mask_high << 16U) | last_filter.filter_mask_low) == ((0x7F0U << 21U) | 6U));
    assert(ABoxCanPort_Suspend(0U));
    config.mode = ABOX_CAN_LOOPBACK;
    config.filter = (ABoxCanFilter){0x1234567U, 0x1FFFFF00U, 1U, 1U, 1U, 1U};
    assert(ABoxCanPort_Init(0U, &config));
    assert(last_config.working_mode == CAN_LOOPBACK_MODE &&
           (((uint32_t)last_filter.filter_list_high << 16U) | last_filter.filter_list_low) == ((0x1234567U << 3U) | 6U) &&
           (((uint32_t)last_filter.filter_mask_high << 16U) | last_filter.filter_mask_low) == ((0x1FFFFF00U << 3U) | 6U));
    assert(ABoxCanPort_Stop(0U));
    config.filter.id = 0x20000000U; assert(!ABoxCanPort_Init(0U, &config));
    {
        ABoxCanBoardOps ops = { &board_prepares, custom_prepare, NULL };
        unsigned prior;
        assert(!ABoxCanPort_BindBoard(&ops));
        ops.release = custom_release;
        assert(ABoxCanPort_BindBoard(&ops));
        ops.prepare = NULL; /* Binding owns a copy of the callback table. */
        config.mode = ABOX_CAN_NORMAL; config.filter = (ABoxCanFilter){0};
        prior = init_count; board_failure = 1U;
        assert(!ABoxCanPort_Init(0U, &config) && init_count == prior && board_prepares == 1U && !board_releases);
        board_failure = 0U; can_failure = 1U;
        assert(!ABoxCanPort_Init(0U, &config) && board_prepares == 2U && board_releases == 1U);
        can_failure = 0U;
        assert(ABoxCanPort_Init(0U, &config) && !ABoxCanPort_BindBoard(NULL));
        assert(ABoxCanPort_Suspend(0U) && board_releases == 2U && ABoxCanPort_BindBoard(NULL));
    }
    return 0;
}
