#include "abox_can_port.h"
#include "can.h"
#include <string.h>

static uint8_t ready[2];
static CAN_HandleTypeDef *handle(unsigned bus) { return bus ? &hcan2 : &hcan1; }

int ABoxCanPort_Init(unsigned bus, uint32_t bitrate, ABoxCanMode mode)
{
    CAN_HandleTypeDef *h;
    CAN_FilterTypeDef filter = {0};
    uint32_t clock, divisor, prescaler;
    if (bus > 1U || !bitrate || bitrate > UINT32_MAX / 18U ||
        (mode != ABOX_CAN_NORMAL && mode != ABOX_CAN_SILENT_LOOPBACK) || ready[bus] ||
        (!bus && ready[1])) return 0;
    h = handle(bus);
    if (h->State != HAL_CAN_STATE_RESET) return 0;
    clock = HAL_RCC_GetPCLK1Freq(); divisor = bitrate * 18U;
    if (!clock || clock % divisor || !(prescaler = clock / divisor) || prescaler > 1024U) return 0;
    h->Instance = bus ? CAN2 : CAN1;
    h->Init.Prescaler = prescaler;
    h->Init.Mode = mode == ABOX_CAN_NORMAL ? CAN_MODE_NORMAL : CAN_MODE_SILENT_LOOPBACK;
    h->Init.SyncJumpWidth = CAN_SJW_1TQ;
    h->Init.TimeSeg1 = CAN_BS1_15TQ; h->Init.TimeSeg2 = CAN_BS2_2TQ;
    h->Init.TimeTriggeredMode = DISABLE; h->Init.AutoBusOff = DISABLE;
    h->Init.AutoWakeUp = ENABLE; h->Init.AutoRetransmission = ENABLE;
    h->Init.ReceiveFifoLocked = DISABLE; h->Init.TransmitFifoPriority = DISABLE;
    if (HAL_CAN_Init(h) != HAL_OK) { (void)HAL_CAN_DeInit(h); return 0; }
    filter.FilterBank = bus ? 14U : 0U; filter.SlaveStartFilterBank = 14U;
    filter.FilterMode = CAN_FILTERMODE_IDMASK; filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0; filter.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(h, &filter) != HAL_OK || HAL_CAN_Start(h) != HAL_OK) {
        (void)HAL_CAN_DeInit(h); return 0;
    }
    ready[bus] = 1U; return 1;
}

int ABoxCanPort_Stop(unsigned bus)
{
    CAN_HandleTypeDef *h;
    int stopped, deinitialized;
    if (bus > 1U || !ready[bus] || (!bus && ready[1])) return 0;
    h = handle(bus);
    stopped = HAL_CAN_Stop(h) == HAL_OK;
    deinitialized = HAL_CAN_DeInit(h) == HAL_OK;
    if (deinitialized) ready[bus] = 0U;
    return stopped && deinitialized;
}

int ABoxCanPort_Send(unsigned bus, const ABoxCanFrame *frame)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;
    if (bus > 1U || !ready[bus] || !frame || frame->length > 8U ||
        frame->extended > 1U || frame->remote > 1U ||
        frame->id > (frame->extended ? 0x1FFFFFFFU : 0x7FFU)) return -1;
    if (!HAL_CAN_GetTxMailboxesFreeLevel(handle(bus))) return 0;
    header.IDE = frame->extended ? CAN_ID_EXT : CAN_ID_STD;
    header.RTR = frame->remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    if (frame->extended) header.ExtId = frame->id; else header.StdId = frame->id;
    header.DLC = frame->length; header.TransmitGlobalTime = DISABLE;
    return HAL_CAN_AddTxMessage(handle(bus), &header, (uint8_t *)frame->data, &mailbox) == HAL_OK ? 1 : -1;
}

int ABoxCanPort_Receive(unsigned bus, ABoxCanFrame *frame)
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
    if (bus > 1U || !ready[bus] || !frame) return -1;
    if (!HAL_CAN_GetRxFifoFillLevel(handle(bus), CAN_RX_FIFO0)) return 0;
    if (HAL_CAN_GetRxMessage(handle(bus), CAN_RX_FIFO0, &header, data) != HAL_OK || header.DLC > 8U ||
        (header.IDE != CAN_ID_STD && header.IDE != CAN_ID_EXT) ||
        (header.RTR != CAN_RTR_DATA && header.RTR != CAN_RTR_REMOTE)) return -1;
    memset(frame, 0, sizeof(*frame)); frame->extended = header.IDE == CAN_ID_EXT;
    frame->remote = header.RTR == CAN_RTR_REMOTE;
    frame->id = frame->extended ? header.ExtId : header.StdId; frame->length = (uint8_t)header.DLC;
    if (!frame->remote) memcpy(frame->data, data, frame->length);
    return 1;
}
