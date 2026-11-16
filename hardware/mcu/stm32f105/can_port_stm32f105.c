#include "abox_can_port.h"
#include "abox_can_filter.h"
#include "abox_can_board_backend.h"
#include "can.h"
#include <string.h>
#ifdef ABOX_CAN_EVENTS_ENABLED
#include "abox_can_events.h"
#endif

static uint8_t ready[2];
static uint8_t suspended[2];
static ABoxCanMode active_mode[2];
static CAN_HandleTypeDef *handle(unsigned bus) { return bus ? &hcan2 : &hcan1; }
int ABoxCanPort_IsReady(unsigned bus) { return bus < 2U && ready[bus]; }
static int filter_config(unsigned bus,unsigned slot,const ABoxCanFilter *rule,unsigned fifo)
{
    CAN_FilterTypeDef filter={0};uint32_t id=0U,mask=0U;
    if (bus>1U || slot>=14U || fifo>1U || (rule && !ABoxCanFilter_Encode(rule,&id,&mask))) return 0;
    filter.FilterBank=(bus ? 14U : 0U)+slot;filter.SlaveStartFilterBank=14U;
    filter.FilterMode=CAN_FILTERMODE_IDMASK;filter.FilterScale=CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment=fifo ? CAN_FILTER_FIFO1 : CAN_FILTER_FIFO0;
    filter.FilterActivation=rule ? ENABLE : DISABLE;
    filter.FilterIdHigh=id>>16U;filter.FilterIdLow=id&0xffffU;
    filter.FilterMaskIdHigh=mask>>16U;filter.FilterMaskIdLow=mask&0xffffU;
    return HAL_CAN_ConfigFilter(handle(bus),&filter)==HAL_OK;
}
int ABoxCanPort_SetFilter(unsigned bus,unsigned slot,const ABoxCanFilter *rule,unsigned fifo)
{ return bus<2U && ready[bus] && filter_config(bus,slot,rule,fifo); }

int ABoxCanPort_Init(unsigned bus, const ABoxCanConfig *settings)
{
    CAN_HandleTypeDef *h;
    CAN_FilterTypeDef filter = {0};
    uint32_t clock, divisor, prescaler, quanta, filter_id, filter_mask;
    if (!settings || settings->options.auto_retransmit > 1U ||
        settings->options.auto_bus_off_recovery > 1U || settings->options.rx_fifo_overwrite > 1U ||
        settings->segment1 < 1U || settings->segment1 > 16U ||
        settings->segment2 < 1U || settings->segment2 > 8U ||
        settings->sync_jump_width < 1U || settings->sync_jump_width > 4U ||
        settings->sync_jump_width > settings->segment2 || bus > 1U || !settings->bitrate ||
        (unsigned)settings->mode > ABOX_CAN_SILENT_LOOPBACK ||
        !ABoxCanFilter_Encode(&settings->filter, &filter_id, &filter_mask) || ready[bus] ||
        (!bus && ready[1]) || (bus && suspended[0])) return 0;
    h = handle(bus);
    if (h->State != HAL_CAN_STATE_RESET && !suspended[bus]) return 0;
    quanta = 1U + settings->segment1 + settings->segment2;
    if (settings->bitrate > UINT32_MAX / quanta) return 0;
    clock = HAL_RCC_GetPCLK1Freq(); divisor = settings->bitrate * quanta;
    if (!clock || clock % divisor || !(prescaler = clock / divisor) || prescaler > 1024U) return 0;
    if (!ABoxCanBoard_Prepare(bus,settings->mode)) return 0;
    h->Instance = bus ? CAN2 : CAN1;
    h->Init.Prescaler = prescaler;
    h->Init.Mode = settings->mode == ABOX_CAN_NORMAL ? CAN_MODE_NORMAL :
                   settings->mode == ABOX_CAN_LOOPBACK ? CAN_MODE_LOOPBACK :
                   settings->mode == ABOX_CAN_SILENT ? CAN_MODE_SILENT : CAN_MODE_SILENT_LOOPBACK;
    h->Init.SyncJumpWidth = (settings->sync_jump_width - 1U) * CAN_SJW_2TQ;
    h->Init.TimeSeg1 = (settings->segment1 - 1U) * CAN_BS1_2TQ;
    h->Init.TimeSeg2 = (settings->segment2 - 1U) * CAN_BS2_2TQ;
    h->Init.TimeTriggeredMode = DISABLE;
    h->Init.AutoBusOff = settings->options.auto_bus_off_recovery ? ENABLE : DISABLE;
    h->Init.AutoWakeUp = ENABLE; h->Init.AutoRetransmission = settings->options.auto_retransmit ? ENABLE : DISABLE;
    h->Init.ReceiveFifoLocked = settings->options.rx_fifo_overwrite ? DISABLE : ENABLE;
    h->Init.TransmitFifoPriority = DISABLE;
    if (suspended[bus]) {
        /* Suspend preserves MSP ownership; restore clocks/reset without
         * incrementing the application's shared CAN1 clock reference. */
        __HAL_RCC_CAN1_CLK_ENABLE();
        if (bus) { __HAL_RCC_CAN2_CLK_ENABLE(); __HAL_RCC_CAN2_RELEASE_RESET(); }
        else __HAL_RCC_CAN1_RELEASE_RESET();
        CLEAR_BIT(h->Instance->MCR, CAN_MCR_SLEEP);
    }
    if (HAL_CAN_Init(h) != HAL_OK) {
        if (HAL_CAN_DeInit(h) == HAL_OK) suspended[bus] = 0U;
        ABoxCanBoard_Release(bus,settings->mode);return 0;
    }
    for (unsigned slot=1U;slot<14U;++slot) {
        if (!filter_config(bus,slot,0,0U)) {
            if (HAL_CAN_DeInit(h)==HAL_OK) suspended[bus]=0U;
            ABoxCanBoard_Release(bus,settings->mode);return 0;
        }
    }
    filter.FilterBank = bus ? 14U : 0U; filter.SlaveStartFilterBank = 14U;
    filter.FilterMode = CAN_FILTERMODE_IDMASK; filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0; filter.FilterActivation = ENABLE;
    filter.FilterIdHigh = filter_id >> 16U; filter.FilterIdLow = filter_id & 0xFFFFU;
    filter.FilterMaskIdHigh = filter_mask >> 16U; filter.FilterMaskIdLow = filter_mask & 0xFFFFU;
    if (HAL_CAN_ConfigFilter(h, &filter) != HAL_OK || HAL_CAN_Start(h) != HAL_OK) {
        if (HAL_CAN_DeInit(h) == HAL_OK) suspended[bus] = 0U;
        ABoxCanBoard_Release(bus,settings->mode);return 0;
    }
    suspended[bus] = 0U; active_mode[bus]=settings->mode;ready[bus] = 1U; return 1;
}

int ABoxCanPort_Suspend(unsigned bus)
{
    if (bus > 1U || !ready[bus] || (!bus && ready[1])) return 0;
#ifdef ABOX_CAN_EVENTS_ENABLED
    ABoxCanPort_DisableEventIrq(bus);
#endif
    ABoxCanBoard_Release(bus,active_mode[bus]);
    /* No HAL Stop/DeInit: their timeout loops require interrupts, while the
     * power-off owner can hold a scheduler critical section here. */
    if (bus) {
        __HAL_RCC_CAN2_FORCE_RESET();
        __HAL_RCC_CAN2_CLK_DISABLE();
        HAL_NVIC_ClearPendingIRQ(CAN2_TX_IRQn); HAL_NVIC_ClearPendingIRQ(CAN2_RX0_IRQn);
        HAL_NVIC_ClearPendingIRQ(CAN2_RX1_IRQn); HAL_NVIC_ClearPendingIRQ(CAN2_SCE_IRQn);
    } else {
        __HAL_RCC_CAN1_FORCE_RESET();
        __HAL_RCC_CAN1_CLK_DISABLE();
        HAL_NVIC_ClearPendingIRQ(CAN1_TX_IRQn); HAL_NVIC_ClearPendingIRQ(CAN1_RX0_IRQn);
        HAL_NVIC_ClearPendingIRQ(CAN1_RX1_IRQn); HAL_NVIC_ClearPendingIRQ(CAN1_SCE_IRQn);
    }
    ready[bus] = 0U; suspended[bus] = 1U;
    return 1;
}

int ABoxCanPort_Stop(unsigned bus)
{
    CAN_HandleTypeDef *h;
    int stopped, deinitialized;
    if (bus > 1U || !ready[bus] || (!bus && ready[1])) return 0;
#ifdef ABOX_CAN_EVENTS_ENABLED
    ABoxCanPort_DisableEventIrq(bus);
#endif
    h = handle(bus);
    stopped = HAL_CAN_Stop(h) == HAL_OK;
    deinitialized = HAL_CAN_DeInit(h) == HAL_OK;
    if (deinitialized) { ABoxCanBoard_Release(bus,active_mode[bus]);ready[bus] = 0U; }
    return stopped && deinitialized;
}

int ABoxCanPort_Send(unsigned bus, const ABoxCanFrame *frame)
{
    uint8_t mailbox;
    return ABoxCanPort_SendTracked(bus, frame, &mailbox);
}

int ABoxCanPort_SendTracked(unsigned bus, const ABoxCanFrame *frame, uint8_t *selected_mailbox)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;
    if (bus > 1U || !ready[bus] || !frame || !selected_mailbox || frame->length > 8U ||
        frame->extended > 1U || frame->remote > 1U ||
        frame->id > (frame->extended ? 0x1FFFFFFFU : 0x7FFU)) return -1;
    if (!HAL_CAN_GetTxMailboxesFreeLevel(handle(bus))) return 0;
    header.IDE = frame->extended ? CAN_ID_EXT : CAN_ID_STD;
    header.RTR = frame->remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    if (frame->extended) header.ExtId = frame->id; else header.StdId = frame->id;
    header.DLC = frame->length; header.TransmitGlobalTime = DISABLE;
    if (HAL_CAN_AddTxMessage(handle(bus), &header, (uint8_t *)frame->data, &mailbox) != HAL_OK) return -1;
    if (mailbox == 1U) *selected_mailbox = 0U;
    else if (mailbox == 2U) *selected_mailbox = 1U;
    else if (mailbox == 4U) *selected_mailbox = 2U;
    else return -1;
    return 1;
}

int ABoxCanPort_ReceiveFifo(unsigned bus,unsigned fifo,ABoxCanFrame *frame)
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
    uint32_t selected=fifo ? CAN_RX_FIFO1 : CAN_RX_FIFO0;
    if (bus > 1U || fifo>1U || !ready[bus] || !frame) return -1;
    if (!HAL_CAN_GetRxFifoFillLevel(handle(bus), selected)) return 0;
    if (HAL_CAN_GetRxMessage(handle(bus), selected, &header, data) != HAL_OK || header.DLC > 8U ||
        (header.IDE != CAN_ID_STD && header.IDE != CAN_ID_EXT) ||
        (header.RTR != CAN_RTR_DATA && header.RTR != CAN_RTR_REMOTE)) return -1;
    memset(frame, 0, sizeof(*frame)); frame->extended = header.IDE == CAN_ID_EXT;
    frame->remote = header.RTR == CAN_RTR_REMOTE;
    frame->id = frame->extended ? header.ExtId : header.StdId; frame->length = (uint8_t)header.DLC;
    if (!frame->remote) memcpy(frame->data, data, frame->length);
    return 1;
}
int ABoxCanPort_Receive(unsigned bus,ABoxCanFrame *frame)
{ return ABoxCanPort_ReceiveFifo(bus,0U,frame); }
