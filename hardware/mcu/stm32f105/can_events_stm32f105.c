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
static void clear_error(void *context)
{
    CAN_HandleTypeDef *h = handle(context);
    h->Instance->ESR &= ~(7U << 4U);
    h->Instance->MSR = 1U << 2U;
}

int ABoxCanPort_BindEvents(unsigned bus, ABoxCanEvents *events,
                           const ABoxCanEventCallbacks *callbacks)
{
    ABoxCanEventHardware hardware;
    if (bus >= 2U || !ABoxCanPort_IsReady(bus)) return 0;
    hardware.context = &buses[bus];
    hardware.snapshot = snapshot;
    hardware.clear_tx = clear_tx;
    hardware.send_mailbox = send_mailbox;
    hardware.abort_mask = abort_mask;
    hardware.receive = receive;
    hardware.clear_error = clear_error;
    return ABoxCanEvents_Init(events, &hardware, callbacks);
}

int ABoxCanPort_EnableEventIrq(unsigned bus, ABoxCanEvents *events)
{
    CAN_HandleTypeDef *h;
    if (bus >= 2U || !events || !ABoxCanPort_IsReady(bus) || bound[bus]) return 0;
    h = handle(&buses[bus]);
    bound[bus] = events;
    h->Instance->IER |= 3U | (1U << 8U) | (1U << 9U) | (1U << 10U) |
                        (1U << 11U) | (1U << 15U);
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
