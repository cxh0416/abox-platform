#include "abox_can_events.h"

#define TX_FINISHED 1U
#define TX_SUCCESS 2U
#define TX_ARBITRATION_LOST 4U
#define TX_ERROR 8U
#define TX_EMPTY_SHIFT 26U

static int valid_frame(const ABoxCanFrame *frame)
{
    return frame && frame->length <= 8U && frame->extended <= 1U && frame->remote <= 1U &&
           frame->id <= (frame->extended ? 0x1FFFFFFFU : 0x7FFU);
}

int ABoxCanEvents_Init(ABoxCanEvents *events, const ABoxCanEventHardware *hardware,
                       const ABoxCanEventCallbacks *callbacks)
{
    if (!events || !hardware || !callbacks || !hardware->snapshot || !hardware->clear_tx ||
        !hardware->send_mailbox || !hardware->abort_mask || !callbacks->complete) return 0;
    events->hardware = *hardware;
    events->callbacks = *callbacks;
    return 1;
}

static uint32_t consume(ABoxCanEvents *events, const ABoxCanSnapshot *snapshot, uint8_t mask)
{
    uint8_t completed = 0U, i;
    uint32_t errors = 0U;
    for (i = 0U; i < 3U; ++i)
        if ((mask & (1U << i)) && (snapshot->tx_status & (TX_FINISHED << (8U * i))))
            completed |= (uint8_t)(1U << i);
    if (!completed) return 0U;
    if (events->callbacks.observe)
        events->callbacks.observe(events->callbacks.context, snapshot, completed);
    for (i = 0U; i < 3U; ++i) {
        uint32_t bits;
        ABoxCanTxResult result;
        if (!(completed & (1U << i))) continue;
        bits = snapshot->tx_status >> (8U * i);
        result = (bits & TX_SUCCESS) ? ABOX_CAN_TX_SUCCESS :
                 (bits & TX_ARBITRATION_LOST) ? ABOX_CAN_TX_ARBITRATION_LOST :
                 (bits & TX_ERROR) ? ABOX_CAN_TX_ERROR : ABOX_CAN_TX_ABORTED;
        if (result == ABOX_CAN_TX_ARBITRATION_LOST) errors |= ABOX_CAN_ERROR_TX_ARBITRATION_0 << (2U * i);
        else if (result == ABOX_CAN_TX_ERROR) errors |= ABOX_CAN_ERROR_TX_FAILURE_0 << (2U * i);
        events->hardware.clear_tx(events->hardware.context, i);
        events->callbacks.complete(events->callbacks.context, i, result);
    }
    return errors;
}

int ABoxCanEvents_Reap(ABoxCanEvents *events)
{
    ABoxCanSnapshot snapshot;
    if (!events || !events->hardware.snapshot ||
        !events->hardware.snapshot(events->hardware.context, &snapshot)) return 0;
    (void)consume(events, &snapshot, 7U);
    return 1;
}

int ABoxCanEvents_Read(ABoxCanEvents *events, ABoxCanSnapshot *snapshot)
{
    return events && snapshot && events->hardware.snapshot &&
           events->hardware.snapshot(events->hardware.context, snapshot);
}

int ABoxCanEvents_Irq(ABoxCanEvents *events)
{
    ABoxCanSnapshot snapshot;
    ABoxCanFrame frame;
    uint32_t errors = 0U, lec;
    if (!ABoxCanEvents_Read(events, &snapshot)) return 0;
    if (events->callbacks.irq_snapshot) events->callbacks.irq_snapshot(events->callbacks.context, &snapshot);
    if (snapshot.interrupt_enable & 1U) errors = consume(events, &snapshot, 7U);
    if ((snapshot.interrupt_enable & 2U) && (snapshot.fifo0_status & 3U) &&
        events->hardware.receive && events->callbacks.receive &&
        events->hardware.receive(events->hardware.context, &frame) == 1)
        events->callbacks.receive(events->callbacks.context, &frame);
    if ((snapshot.interrupt_enable & (1U << 15U)) && (snapshot.interrupt_status & (1U << 2U))) {
        if ((snapshot.interrupt_enable & (1U << 8U)) && (snapshot.error_status & 1U))
            errors |= ABOX_CAN_ERROR_WARNING;
        if ((snapshot.interrupt_enable & (1U << 9U)) && (snapshot.error_status & 2U))
            errors |= ABOX_CAN_ERROR_PASSIVE;
        if ((snapshot.interrupt_enable & (1U << 10U)) && (snapshot.error_status & 4U))
            errors |= ABOX_CAN_ERROR_BUS_OFF;
        lec = (snapshot.error_status >> 4U) & 7U;
        if ((snapshot.interrupt_enable & (1U << 11U)) && lec >= 1U && lec <= 6U)
            errors |= ABOX_CAN_ERROR_STUFF << (lec - 1U);
        if (events->hardware.clear_error) events->hardware.clear_error(events->hardware.context);
    }
    if (errors && events->callbacks.error) events->callbacks.error(events->callbacks.context, &snapshot, errors);
    if (events->callbacks.irq_done) events->callbacks.irq_done(events->callbacks.context);
    return 1;
}

int ABoxCanEvents_Send(ABoxCanEvents *events, const ABoxCanFrame *frame, uint8_t *mailbox)
{
    ABoxCanSnapshot snapshot;
    uint8_t selected;
    if (!events || !mailbox || !valid_frame(frame) || !events->hardware.snapshot ||
        !events->hardware.snapshot(events->hardware.context, &snapshot)) return -1;
    for (selected = 0U; selected < 3U; ++selected)
        if (snapshot.tx_status & (1UL << (TX_EMPTY_SHIFT + selected))) break;
    if (selected == 3U) return 0;
    /* A mailbox observed empty cannot acquire a new completion without a new
     * request; consume its old result before submitting that request. */
    (void)consume(events, &snapshot, (uint8_t)(1U << selected));
    if (!events->hardware.send_mailbox(events->hardware.context, selected, frame)) return -1;
    *mailbox = selected;
    return 1;
}

int ABoxCanEvents_Abort(ABoxCanEvents *events, uint8_t mailbox_mask)
{
    if (!events || !events->hardware.abort_mask || !mailbox_mask || (mailbox_mask & ~7U)) return 0;
    /* The backend writes only abort request bits, never a read-modify-write
     * that could clear a completion which arrived after our snapshot. */
    events->hardware.abort_mask(events->hardware.context, mailbox_mask);
    return 1;
}
