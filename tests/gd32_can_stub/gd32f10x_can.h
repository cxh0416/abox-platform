#ifndef TEST_GD32F10X_CAN_H
#define TEST_GD32F10X_CAN_H
#include <stdint.h>
typedef struct {
    uint8_t working_mode, resync_jump_width, time_segment_1, time_segment_2;
    uint8_t time_triggered, auto_bus_off_recovery, auto_wake_up, auto_retrans, rec_fifo_overwrite, trans_fifo_order;
    uint16_t prescaler;
} can_parameter_struct;
typedef struct { uint32_t tx_sfid, tx_efid; uint8_t tx_ff, tx_ft, tx_dlen, tx_data[64]; } can_trasnmit_message_struct;
typedef struct { uint32_t rx_sfid, rx_efid; uint8_t rx_ff, rx_ft, rx_dlen, rx_data[64], rx_fi; } can_receive_message_struct;
typedef struct {
    uint16_t filter_list_high, filter_list_low, filter_mask_high, filter_mask_low;
    uint16_t filter_fifo_number, filter_number, filter_mode, filter_bits;
    uint8_t filter_enable;
} can_filter_parameter_struct;
#define CAN_INIT_STRUCT 0U
#define CAN_FILTER_STRUCT 1U
#define CAN_TX_MESSAGE_STRUCT 2U
#define CAN_RX_MESSAGE_STRUCT 3U
#define CAN_NORMAL_MODE 0U
#define CAN_SILENT_LOOPBACK_MODE 3U
#define CAN_BT_SJW_1TQ 0U
#define CAN_BT_BS1_15TQ 14U
#define CAN_BT_BS2_2TQ 1U
#define CAN_FILTERMODE_MASK 0U
#define CAN_FILTERBITS_32BIT 1U
#define CAN_FIFO0 0U
#define CAN_NOMAILBOX 3U
#define CAN_FF_STANDARD 0U
#define CAN_FF_EXTENDED 4U
#define CAN_FT_DATA 0U
#define CAN_FT_REMOTE 2U
void can_deinit(uint32_t peripheral);
void can_struct_para_init(uint32_t type, void *data);
uint32_t can_init(uint32_t peripheral, can_parameter_struct *config);
void can1_filter_start_bank(uint8_t bank);
void can_filter_init(can_filter_parameter_struct *filter);
uint8_t can_message_transmit(uint32_t peripheral, can_trasnmit_message_struct *message);
uint8_t can_receive_message_length_get(uint32_t peripheral, uint8_t fifo);
void can_message_receive(uint32_t peripheral, uint8_t fifo, can_receive_message_struct *message);
#endif
