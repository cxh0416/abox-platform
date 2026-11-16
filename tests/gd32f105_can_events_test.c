#include <assert.h>
#include <stdint.h>
#include "gd32f10x.h"
#include "gd32f10x_can.h"
#include "abox_can_events.h"

uint32_t test_can_registers[2][128];
static uint8_t ready, observed, completed;
static ABoxCanTxResult last_result;
int ABoxCanPort_IsReady(unsigned bus) { return ready && bus < 2U; }
int ABoxCanPort_Receive(unsigned bus, ABoxCanFrame *frame) { (void)bus; (void)frame; return 0; }
int ABoxCanPort_ReceiveFifo(unsigned bus,unsigned fifo,ABoxCanFrame *frame) { (void)bus;(void)fifo;(void)frame;return 0; }
static void observe(void *ctx, const ABoxCanSnapshot *snapshot, uint8_t mask)
{ (void)ctx; assert(snapshot->error_status == 0x123U); observed = mask; }
static void complete(void *ctx, uint8_t mailbox, ABoxCanTxResult result)
{ (void)ctx; completed |= (uint8_t)(1U << mailbox); last_result = result; }
static void on_receive1(void *ctx,const ABoxCanFrame *frame) { (void)ctx;(void)frame; }
static void on_overflow(void *ctx,unsigned fifo) { (void)ctx;(void)fifo; }

int main(void)
{
    ABoxCanEvents events;
    ABoxCanEventCallbacks callbacks = {0, observe, complete};
    ABoxCanFrame frame = {.id = 0x123U, .length = 6U, .data = {1U, 2U, 3U, 4U, 5U, 6U}};
    uint8_t mailbox = 99U;
    assert(!ABoxCanPort_BindEvents(0U, &events, &callbacks));
    ready = 1U;
    assert(ABoxCanPort_BindEvents(0U, &events, &callbacks));
    callbacks.receive_fifo1=on_receive1;callbacks.fifo_overflow=on_overflow;
    assert(ABoxCanPort_EnableEventIrq(0U,&events));
    assert((CAN_INTEN(CAN0)&((1U<<4U)|(1U<<3U)|(1U<<6U)))==((1U<<4U)|(1U<<3U)|(1U<<6U)));
    CAN_RFIFO1(CAN0)=0x11U;events.hardware->clear_fifo_overflow(events.hardware->context,1U);
    assert(CAN_RFIFO1(CAN0)==(1U<<4U));ABoxCanPort_DisableEventIrq(0U);
    CAN_ERR(CAN0) = 0x123U;
    CAN_TSTAT(CAN0) = 1UL << 27U;
    assert(ABoxCanEvents_Send(&events, &frame, &mailbox) == 1 && mailbox == 1U);
    assert(CAN_TMI(CAN0, 1U) == (0x123U << 21U | CAN_TMI_TEN));
    assert(CAN_TMP(CAN0, 1U) == 6U);
    assert(CAN_TMDATA0(CAN0, 1U) == 0x04030201U && CAN_TMDATA1(CAN0, 1U) == 0x00000605U);
    CAN_TSTAT(CAN0) = (1U | 8U) << 8U;
    assert(ABoxCanEvents_Reap(&events));
    assert(observed == 2U && completed == 2U && last_result == ABOX_CAN_TX_ERROR);
    assert(CAN_TSTAT(CAN0) == 1U << 8U); /* W1C write for mailbox 1 only. */
    assert(ABoxCanEvents_Abort(&events, 5U));
    assert(CAN_TSTAT(CAN0) == ((1U << 7U) | (1U << 23U)));
    frame.id = 0x1234567U; frame.extended = 1U; frame.remote = 1U;
    CAN_TSTAT(CAN0) = 1UL << 28U;
    assert(ABoxCanEvents_Send(&events, &frame, &mailbox) == 1 && mailbox == 2U);
    assert(CAN_TMI(CAN0, 2U) == ((frame.id << 3U) | CAN_TMI_FF | CAN_TMI_FT | CAN_TMI_TEN));
    assert(!CAN_TMDATA0(CAN0, 2U) && !CAN_TMDATA1(CAN0, 2U));
    return 0;
}
