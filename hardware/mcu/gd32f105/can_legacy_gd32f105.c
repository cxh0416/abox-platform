#include "abox_gd32_can_legacy.h"
#include <stddef.h>
#include <string.h>

_Static_assert(offsetof(ABoxGdCanRegisters, sTxMailBox) == 0x180U, "F105 CAN mailbox offset");
CAN_HandleTypeDef hcan1 = {CAN1, 0U}, hcan2 = {CAN2, 0U};

typedef struct {
    unsigned bus;
    CAN_HandleTypeDef *handle;
    ABoxCanEvents events;
    ABoxCanFrame received;
    uint8_t rx_valid, in_irq;
} LegacyCan;
static LegacyCan ports[2] = {{.bus = 0U, .handle = &hcan1}, {.bus = 1U, .handle = &hcan2}};

__attribute__((weak)) void HAL_CAN_DiagnosticSnapshot(CAN_HandleTypeDef *h, uint32_t ier, uint32_t msr, uint32_t tsr, uint32_t esr)
{ (void)h; (void)ier; (void)msr; (void)tsr; (void)esr; }
__attribute__((weak)) void HAL_CAN_DiagnosticTxReuse(CAN_HandleTypeDef *h, uint32_t tsr, uint32_t mailbox)
{ (void)h; (void)tsr; (void)mailbox; }
__attribute__((weak)) void HAL_CAN_DiagnosticTxAbort(CAN_HandleTypeDef *h, uint32_t tsr)
{ (void)h; (void)tsr; }
__attribute__((weak)) void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h) { (void)h; }
__attribute__((weak)) void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *h) { (void)h; }
__attribute__((weak)) void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *h) { (void)h; }
__attribute__((weak)) void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *h) { (void)h; }
__attribute__((weak)) void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *h) { (void)h; }
__attribute__((weak)) void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *h) { (void)h; }
__attribute__((weak)) void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *h) { (void)h; }
__attribute__((weak)) void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *h) { (void)h; }

static LegacyCan *port(CAN_HandleTypeDef *h)
{ return h == &hcan1 ? &ports[0] : h == &hcan2 ? &ports[1] : NULL; }

static void irq_snapshot(void *context, const ABoxCanSnapshot *snapshot)
{
    LegacyCan *p = context;
    p->in_irq = 1U;
    HAL_CAN_DiagnosticSnapshot(p->handle, snapshot->interrupt_enable, snapshot->interrupt_status,
                               snapshot->tx_status, snapshot->error_status);
}
static void irq_done(void *context) { ((LegacyCan *)context)->in_irq = 0U; }
static void observe(void *context, const ABoxCanSnapshot *snapshot, uint8_t mask)
{
    LegacyCan *p = context;
    if (p->in_irq) return; /* Already observed the complete IRQ snapshot. */
    for (uint8_t i = 0U; i < 3U; ++i)
        if (mask & (1U << i)) HAL_CAN_DiagnosticTxReuse(p->handle, snapshot->tx_status, i);
}
static void complete(void *context, uint8_t mailbox, ABoxCanTxResult result)
{
    LegacyCan *p = context;
    if (!p->in_irq) return; /* Legacy reuse hook has already forwarded success. */
    if (result == ABOX_CAN_TX_SUCCESS) {
        if (mailbox == 0U) HAL_CAN_TxMailbox0CompleteCallback(p->handle);
        else if (mailbox == 1U) HAL_CAN_TxMailbox1CompleteCallback(p->handle);
        else HAL_CAN_TxMailbox2CompleteCallback(p->handle);
    } else if (result == ABOX_CAN_TX_ABORTED) {
        if (mailbox == 0U) HAL_CAN_TxMailbox0AbortCallback(p->handle);
        else if (mailbox == 1U) HAL_CAN_TxMailbox1AbortCallback(p->handle);
        else HAL_CAN_TxMailbox2AbortCallback(p->handle);
    }
}
static void receive(void *context, const ABoxCanFrame *frame)
{
    LegacyCan *p = context;
    p->received = *frame;
    p->rx_valid = 1U;
    HAL_CAN_RxFifo0MsgPendingCallback(p->handle);
    p->rx_valid = 0U;
}
static void error(void *context, const ABoxCanSnapshot *snapshot, uint32_t errors)
{
    LegacyCan *p = context;
    (void)snapshot;
    p->handle->ErrorCode |= errors;
    HAL_CAN_ErrorCallback(p->handle);
}

void CAN_Resume(void)
{
    const ABoxCanOptions options = {0U, 1U, 1U};
    hcan1.ErrorCode = hcan2.ErrorCode = 0U;
    if (!ABoxCanPort_InitWithOptions(0U, 500000U, ABOX_CAN_NORMAL, &options)) Error_Handler();
    if (!ABoxCanPort_InitWithOptions(1U, 500000U, ABOX_CAN_NORMAL, &options)) {
        (void)ABoxCanPort_Stop(0U);
        Error_Handler();
    }
}
void CAN_EnterStandby(void)
{
    if (ABoxCanPort_IsReady(1U)) (void)ABoxCanPort_Stop(1U);
    if (ABoxCanPort_IsReady(0U)) (void)ABoxCanPort_Stop(0U);
}
uint32_t HAL_CAN_Start(CAN_HandleTypeDef *h)
{
    LegacyCan *p = port(h);
    return p && ABoxCanPort_IsReady(p->bus) ? HAL_OK : HAL_ERROR;
}
uint32_t HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t notifications)
{
    LegacyCan *p = port(h);
    ABoxCanEventCallbacks callbacks;
    const uint32_t supported = CAN_IT_TX_MAILBOX_EMPTY | CAN_IT_RX_FIFO0_MSG_PENDING |
        CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE | CAN_IT_ERROR;
    if (!p || notifications != supported) return HAL_ERROR;
    callbacks.context = p;
    callbacks.observe = observe;
    callbacks.complete = complete;
    callbacks.receive = receive;
    callbacks.error = error;
    callbacks.irq_snapshot = irq_snapshot;
    callbacks.irq_done = irq_done;
    if (!ABoxCanPort_BindEvents(p->bus, &p->events, &callbacks) ||
        !ABoxCanPort_EnableEventIrq(p->bus, &p->events)) return HAL_ERROR;
    return HAL_OK;
}
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h)
{
    LegacyCan *p = port(h);
    uint32_t bits, count = 0U;
    if (!p || !ABoxCanPort_IsReady(p->bus)) return 0U;
    bits = h->Instance->TSR >> 26U;
    for (uint8_t i = 0U; i < 3U; ++i) count += (bits >> i) & 1U;
    return count;
}
uint32_t HAL_CAN_IsTxMessagePending(CAN_HandleTypeDef *h, uint32_t mailboxes)
{
    LegacyCan *p = port(h);
    if (!p || !ABoxCanPort_IsReady(p->bus)) return 0U;
    return ((~(h->Instance->TSR >> 26U)) & mailboxes & 7U) != 0U;
}
uint32_t HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *header,
                              const uint8_t *data, uint32_t *mailbox)
{
    LegacyCan *p = port(h);
    ABoxCanFrame frame = {0};
    uint8_t selected;
    if (!p || !header || !data || !mailbox || header->DLC > 8U ||
        (header->IDE != CAN_ID_STD && header->IDE != CAN_ID_EXT) ||
        (header->RTR != CAN_RTR_DATA && header->RTR != CAN_RTR_REMOTE)) return HAL_ERROR;
    frame.extended = header->IDE == CAN_ID_EXT;
    frame.remote = header->RTR == CAN_RTR_REMOTE;
    frame.id = frame.extended ? header->ExtId : header->StdId;
    frame.length = (uint8_t)header->DLC;
    if (!frame.remote) memcpy(frame.data, data, frame.length);
    if (ABoxCanEvents_Send(&p->events, &frame, &selected) != 1) return HAL_ERROR;
    *mailbox = 1U << selected;
    return HAL_OK;
}
uint32_t HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailboxes)
{
    LegacyCan *p = port(h);
    ABoxCanSnapshot snapshot;
    if (!p || !mailboxes || (mailboxes & ~7U) || !ABoxCanPort_IsReady(p->bus)) return HAL_ERROR;
    if (!ABoxCanEvents_Read(&p->events, &snapshot)) return HAL_ERROR;
    HAL_CAN_DiagnosticTxAbort(h, snapshot.tx_status);
    for (uint8_t i = 0U; i < 3U; ++i)
        if (snapshot.tx_status & (CAN_TSR_RQCP0 << (8U * i)))
            h->Instance->TSR = CAN_TSR_RQCP0 << (8U * i);
    return ABoxCanEvents_Abort(&p->events, (uint8_t)mailboxes) ? HAL_OK : HAL_ERROR;
}
uint32_t HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo,
                              CAN_RxHeaderTypeDef *header, uint8_t *data)
{
    LegacyCan *p = port(h);
    if (!p || !p->rx_valid || fifo != CAN_RX_FIFO0 || !header || !data) return HAL_ERROR;
    memset(header, 0, sizeof(*header));
    header->IDE = p->received.extended ? CAN_ID_EXT : CAN_ID_STD;
    header->RTR = p->received.remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    if (p->received.extended) header->ExtId = p->received.id; else header->StdId = p->received.id;
    header->DLC = p->received.length;
    memcpy(data, p->received.data, p->received.length);
    p->rx_valid = 0U;
    return HAL_OK;
}
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h) { return port(h) ? h->ErrorCode : 0U; }
void HAL_CAN_IRQHandler(CAN_HandleTypeDef *h)
{ LegacyCan *p = port(h); if (p) ABoxCanPort_DispatchEventIrq(p->bus); }
