#include "abox_can_events.h"
#include "gd32f10x.h"
#include "gd32f10x_can.h"
#if defined(ABOX_CAN_EVENTS_ENABLED) && defined(ABOX_GD_FREERTOS)
#include "FreeRTOS.h"
#if configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY == 0
#error "CAN IRQ callbacks require a nonzero FreeRTOS syscall interrupt priority"
#endif
#define ABOX_CAN_RX_IRQ_PRIORITY configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#define ABOX_CAN_TX_IRQ_PRIORITY ((configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY < configLIBRARY_LOWEST_INTERRUPT_PRIORITY) ? (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1U) : configLIBRARY_LOWEST_INTERRUPT_PRIORITY)
#else
#define ABOX_CAN_RX_IRQ_PRIORITY 5U
#define ABOX_CAN_TX_IRQ_PRIORITY 6U
#endif

static unsigned buses[2] = {0U, 1U};
static ABoxCanEvents *bound[2];
static uint32_t instance(void *context) { return *(unsigned *)context ? CAN1 : CAN0; }

static int snapshot(void *context, ABoxCanSnapshot *out)
{
    unsigned bus = *(unsigned *)context;
    uint32_t peripheral = instance(context);
    if (!out || !ABoxCanPort_IsReady(bus)) return 0;
    out->error_status = CAN_ERR(peripheral);
    out->tx_status = CAN_TSTAT(peripheral);
    out->interrupt_enable = CAN_INTEN(peripheral);
    out->interrupt_status = CAN_STAT(peripheral);
    out->fifo0_status = CAN_RFIFO0(peripheral);
    out->fifo1_status = CAN_RFIFO1(peripheral);
    for (uint8_t i = 0U; i < 3U; ++i) {
        out->mailbox[i][0] = CAN_TMI(peripheral, i);
        out->mailbox[i][1] = CAN_TMP(peripheral, i);
        out->mailbox[i][2] = CAN_TMDATA0(peripheral, i);
        out->mailbox[i][3] = CAN_TMDATA1(peripheral, i);
    }
    return 1;
}

static void clear_tx(void *context, uint8_t mailbox)
{ CAN_TSTAT(instance(context)) = CAN_TSTAT_MTF0 << (8U * mailbox); }

static uint32_t pack(const uint8_t *data, uint8_t length, uint8_t first)
{
    uint32_t result = 0U;
    for (uint8_t i = 0U; i < 4U && first + i < length; ++i)
        result |= (uint32_t)data[first + i] << (8U * i);
    return result;
}

static int send_mailbox(void *context, uint8_t mailbox, const ABoxCanFrame *frame)
{
    uint32_t peripheral = instance(context);
    uint32_t identifier;
    if (!frame || mailbox >= 3U || !ABoxCanPort_IsReady(*(unsigned *)context)) return 0;
    identifier = frame->extended ? ((frame->id << 3U) | CAN_TMI_FF) : (frame->id << 21U);
    if (frame->remote) identifier |= CAN_TMI_FT;
    CAN_TMI(peripheral, mailbox) = identifier;
    CAN_TMP(peripheral, mailbox) = frame->length;
    CAN_TMDATA0(peripheral, mailbox) = frame->remote ? 0U : pack(frame->data, frame->length, 0U);
    CAN_TMDATA1(peripheral, mailbox) = frame->remote ? 0U : pack(frame->data, frame->length, 4U);
    CAN_TMI(peripheral, mailbox) = identifier | CAN_TMI_TEN;
    return 1;
}

static void abort_mask(void *context, uint8_t mask)
{
    uint32_t command = 0U;
    for (uint8_t i = 0U; i < 3U; ++i)
        if (mask & (1U << i)) command |= CAN_TSTAT_MST0 << (8U * i);
    /* TSTAT completion flags are write-one-to-clear: never use |= here. */
    CAN_TSTAT(instance(context)) = command;
}

static int receive(void *context, ABoxCanFrame *frame)
{ return ABoxCanPort_Receive(*(unsigned *)context, frame); }
static int receive_fifo1(void *context,ABoxCanFrame *frame)
{ return ABoxCanPort_ReceiveFifo(*(unsigned *)context,1U,frame); }
static void clear_fifo_overflow(void *context,unsigned fifo)
{ if (fifo) CAN_RFIFO1(instance(context))=1U<<4U; else CAN_RFIFO0(instance(context))=1U<<4U; }
static void clear_error(void *context)
{
    uint32_t peripheral = instance(context);
    CAN_ERR(peripheral) &= ~CAN_ERR_ERRN;
    CAN_STAT(peripheral) = CAN_STAT_ERRIF;
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
    uint32_t peripheral;
    if (bus >= 2U || !events || !events->callbacks || !events->hardware || !ABoxCanPort_IsReady(bus) || bound[bus]) return 0;
    peripheral = bus ? CAN1 : CAN0;
    bound[bus] = events;
    CAN_INTEN(peripheral) |= CAN_INTEN_TMEIE | CAN_INTEN_RFNEIE0 | CAN_INTEN_WERRIE |
                             CAN_INTEN_PERRIE | CAN_INTEN_BOIE | CAN_INTEN_ERRNIE | CAN_INTEN_ERRIE;
    if (events->callbacks->receive_fifo1) CAN_INTEN(peripheral) |= 1U<<4U;
    if (events->callbacks->fifo_overflow) CAN_INTEN(peripheral) |= (1U<<3U)|(1U<<6U);
#ifdef ABOX_CAN_EVENTS_ENABLED
    NVIC_SetPriority(bus ? CAN1_TX_IRQn : CAN0_TX_IRQn, ABOX_CAN_TX_IRQ_PRIORITY);
    NVIC_SetPriority(bus ? CAN1_RX0_IRQn : CAN0_RX0_IRQn, ABOX_CAN_RX_IRQ_PRIORITY);
    NVIC_SetPriority(bus ? CAN1_RX1_IRQn : CAN0_RX1_IRQn, ABOX_CAN_RX_IRQ_PRIORITY);
    NVIC_SetPriority(bus ? CAN1_EWMC_IRQn : CAN0_EWMC_IRQn, ABOX_CAN_RX_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(bus ? CAN1_TX_IRQn : CAN0_TX_IRQn);
    NVIC_ClearPendingIRQ(bus ? CAN1_RX0_IRQn : CAN0_RX0_IRQn);
    NVIC_ClearPendingIRQ(bus ? CAN1_RX1_IRQn : CAN0_RX1_IRQn);
    NVIC_ClearPendingIRQ(bus ? CAN1_EWMC_IRQn : CAN0_EWMC_IRQn);
    NVIC_EnableIRQ(bus ? CAN1_TX_IRQn : CAN0_TX_IRQn);
    NVIC_EnableIRQ(bus ? CAN1_RX0_IRQn : CAN0_RX0_IRQn);
    NVIC_EnableIRQ(bus ? CAN1_RX1_IRQn : CAN0_RX1_IRQn);
    NVIC_EnableIRQ(bus ? CAN1_EWMC_IRQn : CAN0_EWMC_IRQn);
#endif
    return 1;
}

void ABoxCanPort_DisableEventIrq(unsigned bus)
{
    if (bus >= 2U) return;
    if (ABoxCanPort_IsReady(bus)) CAN_INTEN(bus ? CAN1 : CAN0) = 0U;
#ifdef ABOX_CAN_EVENTS_ENABLED
    NVIC_DisableIRQ(bus ? CAN1_TX_IRQn : CAN0_TX_IRQn);
    NVIC_DisableIRQ(bus ? CAN1_RX0_IRQn : CAN0_RX0_IRQn);
    NVIC_DisableIRQ(bus ? CAN1_RX1_IRQn : CAN0_RX1_IRQn);
    NVIC_DisableIRQ(bus ? CAN1_EWMC_IRQn : CAN0_EWMC_IRQn);
    NVIC_ClearPendingIRQ(bus ? CAN1_TX_IRQn : CAN0_TX_IRQn);
    NVIC_ClearPendingIRQ(bus ? CAN1_RX0_IRQn : CAN0_RX0_IRQn);
    NVIC_ClearPendingIRQ(bus ? CAN1_RX1_IRQn : CAN0_RX1_IRQn);
    NVIC_ClearPendingIRQ(bus ? CAN1_EWMC_IRQn : CAN0_EWMC_IRQn);
#endif
    bound[bus] = 0;
}

void ABoxCanPort_DispatchEventIrq(unsigned bus)
{
    if (bus < 2U && bound[bus]) (void)ABoxCanEvents_Irq(bound[bus]);
}
