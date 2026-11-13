#include <assert.h>
#include "can.h"
#include "abox_can_events.h"

CAN_HandleTypeDef hcan1, hcan2;
static CAN_TypeDef registers;
static uint8_t completed;
int ABoxCanPort_IsReady(unsigned bus) { return bus == 1U; }
int ABoxCanPort_Receive(unsigned bus, ABoxCanFrame *frame) { (void)bus; (void)frame; return 0; }
static void complete(void *ctx, uint8_t mailbox, ABoxCanTxResult result)
{ (void)ctx; assert(result == ABOX_CAN_TX_ABORTED); completed = (uint8_t)(1U << mailbox); }

int main(void)
{
    ABoxCanEvents events;
    ABoxCanEventCallbacks callbacks = {0, 0, complete};
    ABoxCanFrame frame = {.id = 0x321U, .length = 2U, .data = {0x34U, 0x56U}};
    uint8_t mailbox = 99U;
    hcan2.Instance = &registers;
    assert(!ABoxCanPort_BindEvents(0U, &events, &callbacks));
    assert(ABoxCanPort_BindEvents(1U, &events, &callbacks));
    registers.TSR = 1UL << 26U;
    assert(ABoxCanEvents_Send(&events, &frame, &mailbox) == 1 && mailbox == 0U);
    assert(registers.sTxMailBox[0].TIR == ((0x321U << 21U) | 1U));
    assert(registers.sTxMailBox[0].TDTR == 2U && registers.sTxMailBox[0].TDLR == 0x5634U);
    registers.TSR = 1U << 16U;
    assert(ABoxCanEvents_Reap(&events) && completed == 4U);
    assert(registers.TSR == 1U << 16U);
    assert(ABoxCanEvents_Abort(&events, 3U));
    assert(registers.TSR == ((1U << 7U) | (1U << 15U)));
    return 0;
}
