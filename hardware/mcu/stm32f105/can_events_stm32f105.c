#include "abox_can_events.h"
#include "can.h"

static unsigned buses[2] = {0U, 1U};
static ABoxCanEvents *bound[2];
static CAN_HandleTypeDef *handle(void *context) { return *(unsigned *)context ? &hcan2 : &hcan1; }

static int snapshot(void *context, ABoxCanSnapshot *out)
{
    CAN_HandleTypeDef *h = handle(context);
    if (!out || !ABoxCanPort_IsReady(*(unsigned *)context)) return 0;
    out->error_status = h->Instance->ESR;
    out->tx_status = h->Instance->TSR;
    out->interrupt_enable = h->Instance->IER;
    out->interrupt_status = h->Instance->MSR;
    out->fifo0_status = h->Instance->RF0R;
    out->fifo1_status = h->Instance->RF1R;
    for (uint8_t i = 0U; i < 3U; ++i) {
        out->mailbox[i][0] = h->Instance->sTxMailBox[i].TIR;
        out->mailbox[i][1] = h->Instance->sTxMailBox[i].TDTR;
        out->mailbox[i][2] = h->Instance->sTxMailBox[i].TDLR;
        out->mailbox[i][3] = h->Instance->sTxMailBox[i].TDHR;
    }
    return 1;
}

static void clear_tx(void *context, uint8_t mailbox)
{ handle(context)->Instance->TSR = 1UL << (8U * mailbox); }

static uint32_t pack(const uint8_t *data, uint8_t length, uint8_t first)
{
    uint32_t result = 0U;
    for (uint8_t i = 0U; i < 4U && first + i < length; ++i)
        result |= (uint32_t)data[first + i] << (8U * i);
    return result;
}

static int send_mailbox(void *context, uint8_t mailbox, const ABoxCanFrame *frame)
{
    CAN_HandleTypeDef *h = handle(context);
    uint32_t identifier;
    if (!frame || mailbox >= 3U || !ABoxCanPort_IsReady(*(unsigned *)context)) return 0;
    identifier = frame->extended ? ((frame->id << 3U) | 4U) : (frame->id << 21U);
    if (frame->remote) identifier |= 2U;
    h->Instance->sTxMailBox[mailbox].TIR = identifier;
    h->Instance->sTxMailBox[mailbox].TDTR = frame->length;
    h->Instance->sTxMailBox[mailbox].TDLR = frame->remote ? 0U : pack(frame->data, frame->length, 0U);
    h->Instance->sTxMailBox[mailbox].TDHR = frame->remote ? 0U : pack(frame->data, frame->length, 4U);
    h->Instance->sTxMailBox[mailbox].TIR = identifier | 1U;
    return 1;
}

static void abort_mask(void *context, uint8_t mask)
{
    uint32_t command = 0U;
    for (uint8_t i = 0U; i < 3U; ++i)
        if (mask & (1U << i)) command |= 1UL << (7U + 8U * i);
    /* TSR completion flags are write-one-to-clear; never read-modify-write. */
    handle(context)->Instance->TSR = command;
}

static int receive(void *context, ABoxCanFrame *frame)
{ return ABoxCanPort_Receive(*(unsigned *)context, frame); }
static int receive_fifo1(void *context,ABoxCanFrame *frame)
{ return ABoxCanPort_ReceiveFifo(*(unsigned *)context,1U,frame); }
static void clear_fifo_overflow(void *context,unsigned fifo)
{
    CAN_TypeDef *controller=handle(context)->Instance;
    if (fifo) controller->RF1R=1U<<4U; else controller->RF0R=1U<<4U;
}
static void clear_error(void *context)
{
    CAN_HandleTypeDef *h = handle(context);
    h->Instance->ESR &= ~(7U << 4U);
    h->Instance->MSR = 1U << 2U;
}

static const ABoxCanEventHardware hardware[2] = {
    {&buses[0], snapshot, clear_tx, send_mailbox, abort_mask, receive, clear_error, receive_fifo1, clear_fifo_overflow},
    {&buses[1], snapshot, clear_tx, send_mailbox, abort_mask, receive, clear_error, receive_fifo1, clear_fifo_overflow}
};

int ABoxCanPort_BindEvents(unsigned bus, ABoxCanEvents *events,
                           const ABoxCanEventCallbacks *callbacks)
{
    if (bus >= 2U || !ABoxCanPort_IsReady(bus)) return 0;
    return ABoxCanEvents_Init(events, &hardware[bus], callbacks);
}

int ABoxCanPort_EnableEventIrq(unsigned bus, ABoxCanEvents *events)
{
    CAN_HandleTypeDef *h;
    if (bus >= 2U || !events || !events->callbacks || !events->hardware || !ABoxCanPort_IsReady(bus) || bound[bus]) return 0;
    h = handle(&buses[bus]);
    bound[bus] = events;
    h->Instance->IER |= 3U | (1U << 8U) | (1U << 9U) | (1U << 10U) |
                        (1U << 11U) | (1U << 15U);
    if (events->callbacks->receive_fifo1) h->Instance->IER |= 1U<<4U;
    if (events->callbacks->fifo_overflow) h->Instance->IER |= (1U<<3U)|(1U<<6U);
    return 1;
}

void ABoxCanPort_DisableEventIrq(unsigned bus)
{
    if (bus >= 2U) return;
    if (ABoxCanPort_IsReady(bus)) handle(&buses[bus])->Instance->IER = 0U;
    bound[bus] = 0;
}

void ABoxCanPort_DispatchEventIrq(unsigned bus)
{
    if (bus < 2U && bound[bus]) (void)ABoxCanEvents_Irq(bound[bus]);
}
