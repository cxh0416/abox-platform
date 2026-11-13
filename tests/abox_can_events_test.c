#include <assert.h>
#include <string.h>
#include "abox_can_events.h"

typedef struct {
    ABoxCanSnapshot snapshot;
    uint8_t cleared, aborted, sent, observed, completions;
    uint8_t inject_completion, received, error_cleared;
    uint32_t errors;
    ABoxCanTxResult result[3];
} Fake;

static int snapshot(void *context, ABoxCanSnapshot *out)
{ *out = ((Fake *)context)->snapshot; return 1; }
static void clear_tx(void *context, uint8_t mailbox)
{
    Fake *fake = context;
    fake->cleared |= (uint8_t)(1U << mailbox);
    fake->snapshot.tx_status &= ~(15U << (8U * mailbox));
}
static int send_mailbox(void *context, uint8_t mailbox, const ABoxCanFrame *frame)
{
    Fake *fake = context;
    assert(frame->id == 0x123U);
    /* Completion must be observed before this slot is reused. */
    if (mailbox == 0U) assert(fake->cleared & 1U);
    fake->sent = (uint8_t)(1U << mailbox);
    fake->snapshot.tx_status &= ~(1UL << (26U + mailbox));
    if (fake->inject_completion) fake->snapshot.tx_status |= (1UL << 8U) | (1UL << 10U);
    return 1;
}
static void abort_mask(void *context, uint8_t mask)
{ ((Fake *)context)->aborted = mask; }
static int receive(void *context, ABoxCanFrame *frame)
{ (void)context; frame->id = 0x456U; return 1; }
static void clear_error(void *context) { ((Fake *)context)->error_cleared = 1U; }
static void on_receive(void *context, const ABoxCanFrame *frame)
{ assert(frame->id == 0x456U); ((Fake *)context)->received++; }
static void on_error(void *context, const ABoxCanSnapshot *snapshot, uint32_t error_bits)
{ assert(snapshot->error_status == 0x34U); ((Fake *)context)->errors = error_bits; }
static void observe(void *context, const ABoxCanSnapshot *snapshot, uint8_t mask)
{
    Fake *fake = context;
    assert((snapshot->tx_status & 1U) || (snapshot->tx_status & (1U << 8U)));
    fake->observed |= mask;
}
static void complete(void *context, uint8_t mailbox, ABoxCanTxResult result)
{
    Fake *fake = context;
    assert(fake->observed & (1U << mailbox));
    fake->completions |= (uint8_t)(1U << mailbox);
    fake->result[mailbox] = result;
}

int main(void)
{
    Fake fake = {0};
    ABoxCanEvents events;
    ABoxCanFrame frame = {.id = 0x123U, .length = 1U};
    ABoxCanEventHardware hardware = {&fake, snapshot, clear_tx, send_mailbox, abort_mask, receive, clear_error};
    ABoxCanEventCallbacks callbacks = {&fake, observe, complete, on_receive, on_error};
    uint8_t mailbox = 99U;
    assert(ABoxCanEvents_Init(&events, &hardware, &callbacks));
    assert(ABoxCanEvents_Send(&events, &frame, &mailbox) == 0 && mailbox == 99U);
    fake.snapshot.tx_status = (1UL << 26U) | 3U;
    fake.inject_completion = 1U;
    assert(ABoxCanEvents_Send(&events, &frame, &mailbox) == 1 && mailbox == 0U);
    assert(fake.sent == 1U && fake.completions == 1U && fake.result[0] == ABOX_CAN_TX_SUCCESS);
    /* A different mailbox can finish while the selected mailbox is reused. */
    assert(fake.snapshot.tx_status & (1UL << 8U));
    assert(ABoxCanEvents_Reap(&events));
    assert(fake.completions == 3U && fake.result[1] == ABOX_CAN_TX_ARBITRATION_LOST);
    assert(ABoxCanEvents_Abort(&events, 5U) && fake.aborted == 5U);
    assert(!ABoxCanEvents_Abort(&events, 8U));
    fake.snapshot.interrupt_enable = 3U | (1U << 10U) | (1U << 11U) | (1U << 15U);
    fake.snapshot.interrupt_status = 1U << 2U;
    fake.snapshot.fifo0_status = 1U;
    fake.snapshot.error_status = 0x34U; /* Bus-off and ACK error. */
    assert(ABoxCanEvents_Irq(&events));
    assert(fake.received == 1U && fake.error_cleared &&
           fake.errors == (ABOX_CAN_ERROR_BUS_OFF | ABOX_CAN_ERROR_ACK));
    frame.length = 9U;
    assert(ABoxCanEvents_Send(&events, &frame, &mailbox) == -1);
    return 0;
}
