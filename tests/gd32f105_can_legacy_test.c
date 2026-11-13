#include <assert.h>
#include <string.h>
#include "abox_gd32_can_legacy.h"

uint32_t test_can_memory[512];
static uint8_t ready[2], abort_request[2], completed[2], rx_calls, error_calls, reuse_calls;
static unsigned context_bus[2] = {0U, 1U};
static ABoxCanEvents *enabled[2];
static ABoxCanFrame incoming;
static uint32_t interrupt_snapshot_count;

int ABoxCanPort_IsReady(unsigned bus) { return bus < 2U && ready[bus]; }
int ABoxCanPort_InitWithOptions(unsigned bus, uint32_t bitrate, ABoxCanMode mode, const ABoxCanOptions *options)
{
    assert(bus < 2U && !ready[bus] && bitrate == 500000U && mode == ABOX_CAN_NORMAL);
    assert(options && !options->auto_retransmit && options->auto_bus_off_recovery && options->rx_fifo_overwrite);
    ready[bus] = 1U;
    return 1;
}
int ABoxCanPort_Stop(unsigned bus) { assert(ready[bus]); ready[bus] = 0U; enabled[bus] = 0; return 1; }
static CAN_HandleTypeDef *get_handle(unsigned bus) { return bus ? &hcan2 : &hcan1; }
static int snapshot(void *ctx, ABoxCanSnapshot *out)
{
    unsigned bus = *(unsigned *)ctx;
    CAN_HandleTypeDef *h = get_handle(bus);
    if (!ready[bus]) return 0;
    memset(out, 0, sizeof(*out));
    out->error_status = h->Instance->ESR; out->tx_status = h->Instance->TSR;
    out->interrupt_enable = h->Instance->IER; out->interrupt_status = h->Instance->MSR;
    out->fifo0_status = h->Instance->RF0R;
    return 1;
}
static void clear_tx(void *ctx, uint8_t mailbox)
{ get_handle(*(unsigned *)ctx)->Instance->TSR &= ~(15U << (8U * mailbox)); }
static int send_mailbox(void *ctx, uint8_t mailbox, const ABoxCanFrame *frame)
{
    CAN_HandleTypeDef *h = get_handle(*(unsigned *)ctx);
    h->Instance->sTxMailBox[mailbox].TIR = (frame->id << 21U) | 1U;
    h->Instance->TSR &= ~(1UL << (26U + mailbox));
    return 1;
}
static void abort_mask(void *ctx, uint8_t mask) { abort_request[*(unsigned *)ctx] = mask; }
static int read_frame(void *ctx, ABoxCanFrame *frame)
{ (void)ctx; *frame = incoming; return 1; }
static void clear_error(void *ctx) { get_handle(*(unsigned *)ctx)->Instance->MSR = 0U; }
int ABoxCanPort_BindEvents(unsigned bus, ABoxCanEvents *events, const ABoxCanEventCallbacks *callbacks)
{
    ABoxCanEventHardware hardware = {&context_bus[bus], snapshot, clear_tx, send_mailbox,
                                     abort_mask, read_frame, clear_error};
    return ABoxCanEvents_Init(events, &hardware, callbacks);
}
int ABoxCanPort_EnableEventIrq(unsigned bus, ABoxCanEvents *events)
{
    enabled[bus] = events;
    get_handle(bus)->Instance->IER = 3U | (15U << 8U) | (1U << 15U);
    return 1;
}
void ABoxCanPort_DispatchEventIrq(unsigned bus) { if (enabled[bus]) (void)ABoxCanEvents_Irq(enabled[bus]); }
void Error_Handler(void) { assert(0); }
void HAL_CAN_DiagnosticSnapshot(CAN_HandleTypeDef *h, uint32_t ier, uint32_t msr, uint32_t tsr, uint32_t esr)
{ (void)h; (void)ier; (void)msr; (void)tsr; (void)esr; ++interrupt_snapshot_count; }
void HAL_CAN_DiagnosticTxReuse(CAN_HandleTypeDef *h, uint32_t tsr, uint32_t mailbox)
{
    ++reuse_calls;
    if (mailbox == 0U && (tsr & 3U) == 3U) HAL_CAN_TxMailbox0CompleteCallback(h);
}
void HAL_CAN_DiagnosticTxAbort(CAN_HandleTypeDef *h, uint32_t tsr) { (void)h; (void)tsr; }
void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *h) { completed[h == &hcan2] |= 1U; }
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *h) { completed[h == &hcan2] |= 2U; }
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *h) { completed[h == &hcan2] |= 4U; }
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *h) { (void)h; }
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *h) { (void)h; }
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *h) { (void)h; }
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *h) { assert(HAL_CAN_GetError(h) & HAL_CAN_ERROR_BOF); ++error_calls; }
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h)
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
    assert(HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &header, data) == HAL_OK);
    assert(header.StdId == 0x123U && header.DLC == 1U && data[0] == 0xA5U);
    ++rx_calls;
}

int main(void)
{
    const uint32_t notifications = CAN_IT_TX_MAILBOX_EMPTY | CAN_IT_RX_FIFO0_MSG_PENDING |
        CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE | CAN_IT_ERROR;
    CAN_TxHeaderTypeDef header = {.StdId = 0x321U, .DLC = 1U};
    uint8_t data[8] = {0x42U};
    uint32_t mailbox = 99U;
    CAN_Resume();
    assert(HAL_CAN_Start(&hcan1) == HAL_OK && HAL_CAN_Start(&hcan2) == HAL_OK);
    assert(HAL_CAN_ActivateNotification(&hcan1, notifications) == HAL_OK);
    assert(HAL_CAN_ActivateNotification(&hcan2, notifications) == HAL_OK);
    /* An old completion is forwarded once before the mailbox gets a new owner. */
    hcan1.Instance->TSR = (1UL << 26U) | 3U;
    assert(HAL_CAN_AddTxMessage(&hcan1, &header, data, &mailbox) == HAL_OK && mailbox == 1U);
    assert(reuse_calls == 1U && completed[0] == 1U);
    completed[0] = 0U;
    hcan1.Instance->TSR = 3U; hcan1.Instance->RF0R = 1U;
    hcan1.Instance->MSR = 4U; hcan1.Instance->ESR = 4U;
    incoming.id = 0x123U; incoming.length = 1U; incoming.data[0] = 0xA5U;
    HAL_CAN_IRQHandler(&hcan1);
    assert(interrupt_snapshot_count == 1U && completed[0] == 1U && rx_calls == 1U && error_calls == 1U);
    assert(HAL_CAN_AbortTxRequest(&hcan1, 7U) == HAL_OK && abort_request[0] == 7U);
    CAN_EnterStandby();
    assert(!ready[0] && !ready[1]);
    return 0;
}
