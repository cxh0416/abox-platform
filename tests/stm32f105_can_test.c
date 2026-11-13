#include <assert.h>
#include <string.h>
#include "can.h"
#include "abox_can_port.h"

CAN_HandleTypeDef hcan1, hcan2;
static CAN_FilterTypeDef last_filter;
static CAN_TxHeaderTypeDef sent;
static CAN_RxHeaderTypeDef received;
static unsigned free_mailboxes = 1U, fifo_count, send_calls;
static uint8_t tx_bytes[8], rx_bytes[8];
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 36000000U; }
uint32_t HAL_CAN_Init(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_READY; return HAL_OK; }
uint32_t HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f)
{ (void)h; last_filter = *f; return HAL_OK; }
uint32_t HAL_CAN_Start(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_LISTENING; return HAL_OK; }
uint32_t HAL_CAN_Stop(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_READY; return HAL_OK; }
uint32_t HAL_CAN_DeInit(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_RESET; return HAL_OK; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) { (void)h; return free_mailboxes; }
uint32_t HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *header, uint8_t data[8], uint32_t *mailbox)
{ (void)h; sent = *header; memcpy(tx_bytes, data, 8U); *mailbox = 1U; ++send_calls; return HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo)
{ (void)h; assert(fifo == CAN_RX_FIFO0); return fifo_count; }
uint32_t HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t data[8])
{ (void)h; assert(fifo == CAN_RX_FIFO0); *header = received; memcpy(data, rx_bytes, 8U); fifo_count = 0; return HAL_OK; }

int main(void)
{
    ABoxCanFrame frame = {0}, out = {0};
    uint8_t selected = 99U;
    const ABoxCanOptions sweeper = {0U, 1U, 1U}, invalid = {2U, 0U, 0U};
    assert(!ABoxCanPort_InitWithOptions(0U, 500000U, ABOX_CAN_NORMAL, NULL));
    assert(!ABoxCanPort_InitWithOptions(0U, 500000U, ABOX_CAN_NORMAL, &invalid));
    assert(!ABoxCanPort_Init(2U, 250000U, ABOX_CAN_NORMAL));
    assert(!ABoxCanPort_Init(0U, 123456U, ABOX_CAN_NORMAL));
    assert(ABoxCanPort_Init(0U, 250000U, ABOX_CAN_SILENT_LOOPBACK));
    assert(hcan1.Instance == CAN1 && hcan1.Init.Prescaler == 8U && hcan1.Init.Mode == CAN_MODE_SILENT_LOOPBACK);
    assert(hcan1.Init.AutoRetransmission == ENABLE && hcan1.Init.AutoBusOff == DISABLE && hcan1.Init.ReceiveFifoLocked == DISABLE);
    assert(last_filter.FilterBank == 0U && last_filter.SlaveStartFilterBank == 14U);
    assert(ABoxCanPort_Init(1U, 500000U, ABOX_CAN_NORMAL));
    assert(hcan2.Instance == CAN2 && hcan2.Init.Prescaler == 4U && last_filter.FilterBank == 14U);
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
    assert(!ABoxCanPort_Stop(0U) && ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
    assert(ABoxCanPort_InitWithOptions(0U, 500000U, ABOX_CAN_SILENT_LOOPBACK, &sweeper));
    assert(hcan1.Init.AutoRetransmission == DISABLE && hcan1.Init.AutoBusOff == ENABLE && hcan1.Init.ReceiveFifoLocked == DISABLE);
    assert(ABoxCanPort_Stop(0U));
    assert(ABoxCanPort_Send(0U, &frame) == -1);
    hcan1.State = HAL_CAN_STATE_LISTENING;
    assert(!ABoxCanPort_Init(0U, 250000U, ABOX_CAN_NORMAL));
    return 0;
}
