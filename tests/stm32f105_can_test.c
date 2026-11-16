#include <assert.h>
#include <string.h>
#include "can.h"
#include "abox_can_port.h"

CAN_HandleTypeDef hcan1, hcan2;
CAN_TypeDef test_can1, test_can2;
unsigned test_reset_force[2], test_reset_release[2];
static unsigned stop_calls, fail_init;
static CAN_FilterTypeDef last_filter;
static CAN_FilterTypeDef filter_banks[28];
static unsigned selected_fifo;
static CAN_TxHeaderTypeDef sent;
static CAN_RxHeaderTypeDef received;
static unsigned free_mailboxes = 1U, fifo_count, send_calls;
static uint8_t tx_bytes[8], rx_bytes[8];
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 36000000U; }
uint32_t HAL_CAN_Init(CAN_HandleTypeDef *h) { if (fail_init) return 1U; h->State = HAL_CAN_STATE_READY; return HAL_OK; }
uint32_t HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f)
{ (void)h; last_filter = *f; filter_banks[f->FilterBank]=*f; return HAL_OK; }
uint32_t HAL_CAN_Start(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_LISTENING; return HAL_OK; }
uint32_t HAL_CAN_Stop(CAN_HandleTypeDef *h) { ++stop_calls; h->State = HAL_CAN_STATE_READY; return HAL_OK; }
uint32_t HAL_CAN_DeInit(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_RESET; return HAL_OK; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) { (void)h; return free_mailboxes; }
uint32_t HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *header, uint8_t data[8], uint32_t *mailbox)
{ (void)h; sent = *header; memcpy(tx_bytes, data, 8U); *mailbox = 1U; ++send_calls; return HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo)
{ (void)h; selected_fifo=fifo; return fifo_count; }
uint32_t HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t data[8])
{ (void)h; selected_fifo=fifo; *header = received; memcpy(data, rx_bytes, 8U); fifo_count = 0; return HAL_OK; }

int main(void)
{
    ABoxCanFrame frame = {0}, out = {0};
    uint8_t selected = 99U;
    ABoxCanConfig config = {250000U, 15U, 2U, 1U, ABOX_CAN_SILENT_LOOPBACK, {1U, 0U, 1U}, {0}};
    assert(!ABoxCanPort_Init(0U, NULL));
    config.options.auto_retransmit = 2U; assert(!ABoxCanPort_Init(0U, &config));
    config.options.auto_retransmit = 1U;
    assert(!ABoxCanPort_Init(2U, &config));
    config.bitrate = 123456U; assert(!ABoxCanPort_Init(0U, &config));
    config.bitrate = 250000U;
    assert(ABoxCanPort_Init(0U, &config));
    assert(hcan1.Instance == CAN1 && hcan1.Init.Prescaler == 8U && hcan1.Init.Mode == CAN_MODE_SILENT_LOOPBACK);
    assert(hcan1.Init.AutoRetransmission == ENABLE && hcan1.Init.AutoBusOff == DISABLE && hcan1.Init.ReceiveFifoLocked == DISABLE);
    assert(last_filter.FilterBank == 0U && last_filter.SlaveStartFilterBank == 14U);
    config.bitrate = 500000U; config.mode = ABOX_CAN_NORMAL;
    assert(ABoxCanPort_Init(1U, &config));
    assert(hcan2.Instance == CAN2 && hcan2.Init.Prescaler == 4U && last_filter.FilterBank == 14U);
    {
        ABoxCanFilter rule={0x321U,0x7ffU,1U,0U,1U,0U};
        assert(!ABoxCanPort_SetFilter(2U,1U,&rule,0U));
        assert(!ABoxCanPort_SetFilter(1U,14U,&rule,0U));
        assert(!ABoxCanPort_SetFilter(1U,1U,&rule,2U));
        assert(ABoxCanPort_SetFilter(1U,3U,&rule,1U));
        assert(filter_banks[17].FilterActivation==ENABLE && filter_banks[17].FilterFIFOAssignment==CAN_FILTER_FIFO1);
        assert(filter_banks[0].FilterActivation==ENABLE && filter_banks[14].FilterActivation==ENABLE);
        assert(ABoxCanPort_SetFilter(1U,3U,NULL,1U) && filter_banks[17].FilterActivation==DISABLE);
        assert(ABoxCanPort_SetFilter(1U,3U,&rule,1U));
    }
    assert(!ABoxCanPort_Suspend(0U));
    assert(ABoxCanPort_Suspend(1U) && ABoxCanPort_Suspend(0U));
    assert(!stop_calls && test_reset_force[0] == 1U && test_reset_force[1] == 1U);
    assert(!ABoxCanPort_Init(1U, &config));
    fail_init = 1U;
    assert(!ABoxCanPort_Init(0U, &config) && hcan1.State == HAL_CAN_STATE_RESET);
    fail_init = 0U;
    assert(ABoxCanPort_Init(0U, &config) && ABoxCanPort_Init(1U, &config));
    assert(filter_banks[17].FilterActivation==DISABLE);
    assert(test_reset_release[0] == 1U && test_reset_release[1] == 1U);
    frame.id = 0x800U; frame.length = 8U; assert(ABoxCanPort_Send(0U, &frame) == -1 && !send_calls);
    frame.id = 0x1234567U; frame.extended = 1U; frame.length = 2U; frame.data[0] = 0x56U;
    free_mailboxes = 0U; assert(ABoxCanPort_Send(1U, &frame) == 0);
    assert(ABoxCanPort_SendTracked(1U, &frame, &selected) == 0 && selected == 99U);
    free_mailboxes = 1U; assert(ABoxCanPort_Send(1U, &frame) == 1 && send_calls == 1U);
    assert(ABoxCanPort_SendTracked(1U, &frame, &selected) == 1 && selected == 0U);
    assert(ABoxCanPort_SendTracked(1U, &frame, NULL) == -1);
    assert(sent.IDE == CAN_ID_EXT && sent.ExtId == frame.id && sent.DLC == 2U && tx_bytes[0] == 0x56U);
    assert(ABoxCanPort_Receive(1U, &out) == 0);
    fifo_count = 1U; received.IDE = CAN_ID_EXT; received.RTR = CAN_RTR_REMOTE;
    received.ExtId = frame.id; received.DLC = 2U; memset(rx_bytes, 0xAA, sizeof(rx_bytes));
    assert(ABoxCanPort_Receive(1U, &out) == 1 && out.id == frame.id && out.extended && out.remote && out.data[0] == 0U);
    fifo_count = 1U; received.DLC = 9U; assert(ABoxCanPort_Receive(1U, &out) == -1);
    received.DLC=2U;fifo_count=1U;assert(ABoxCanPort_ReceiveFifo(1U,1U,&out)==1 && selected_fifo==CAN_RX_FIFO1);
    assert(ABoxCanPort_ReceiveFifo(1U,2U,&out)==-1);
    assert(!ABoxCanPort_Stop(0U) && ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
    config = (ABoxCanConfig){500000U, 15U, 2U, 1U, ABOX_CAN_SILENT_LOOPBACK, {0U, 1U, 1U}, {0}};
    assert(ABoxCanPort_Init(0U, &config));
    assert(hcan1.Init.AutoRetransmission == DISABLE && hcan1.Init.AutoBusOff == ENABLE && hcan1.Init.ReceiveFifoLocked == DISABLE);
    assert(ABoxCanPort_Stop(0U));
    config = (ABoxCanConfig){250000U, 13U, 2U, 2U, ABOX_CAN_SILENT_LOOPBACK, {1U, 0U, 1U}, {0}};
    assert(ABoxCanPort_Init(0U, &config));
    assert(hcan1.Init.Prescaler == 9U && hcan1.Init.TimeSeg1 == 12U * CAN_BS1_2TQ &&
           hcan1.Init.TimeSeg2 == CAN_BS2_2TQ && hcan1.Init.SyncJumpWidth == CAN_SJW_2TQ);
    assert(ABoxCanPort_Stop(0U));
    config.sync_jump_width = 3U; assert(!ABoxCanPort_Init(0U, &config));
    config.sync_jump_width = 1U; config.segment1 = 0U; assert(!ABoxCanPort_Init(0U, &config));
    assert(ABoxCanPort_Send(0U, &frame) == -1);
    config.segment1 = 15U;
    config.mode = ABOX_CAN_SILENT;
    config.filter = (ABoxCanFilter){0x321U, 0x7F0U, 1U, 0U, 1U, 0U};
    assert(ABoxCanPort_Init(0U, &config));
    assert(hcan1.Init.Mode == CAN_MODE_SILENT &&
           ((last_filter.FilterIdHigh << 16U) | last_filter.FilterIdLow) == (0x321U << 21U) &&
           ((last_filter.FilterMaskIdHigh << 16U) | last_filter.FilterMaskIdLow) == ((0x7F0U << 21U) | 6U));
    assert(ABoxCanPort_Stop(0U));
    config.mode = ABOX_CAN_LOOPBACK;
    config.filter = (ABoxCanFilter){0x1234567U, 0x1FFFFF00U, 1U, 1U, 1U, 1U};
    assert(ABoxCanPort_Init(0U, &config));
    assert(hcan1.Init.Mode == CAN_MODE_LOOPBACK &&
           ((last_filter.FilterIdHigh << 16U) | last_filter.FilterIdLow) == ((0x1234567U << 3U) | 6U) &&
           ((last_filter.FilterMaskIdHigh << 16U) | last_filter.FilterMaskIdLow) == ((0x1FFFFF00U << 3U) | 6U));
    assert(ABoxCanPort_Stop(0U));
    config.filter.id = 0x20000000U; assert(!ABoxCanPort_Init(0U, &config));
    config.filter = (ABoxCanFilter){0};
    hcan1.State = HAL_CAN_STATE_LISTENING;
    config.segment1 = 15U;
    assert(!ABoxCanPort_Init(0U, &config));
    return 0;
}
