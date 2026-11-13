#ifndef ABOX_CAN_EVENTS_H
#define ABOX_CAN_EVENTS_H

#include <stdint.h>
#include "abox_can_port.h"

/* Normalized F105 controller snapshots retain the register fields used by
 * diagnostics. A TX completion occupies four bits per mailbox in tx_status;
 * bits 26..28 indicate empty mailboxes. */
typedef struct {
    uint32_t error_status, tx_status;
    uint32_t interrupt_enable, interrupt_status, fifo0_status;
    uint32_t mailbox[3][4];
} ABoxCanSnapshot;

/* F105 diagnostic error bits, compatible across the supported controllers. */
#define ABOX_CAN_ERROR_WARNING       0x00000001U
#define ABOX_CAN_ERROR_PASSIVE       0x00000002U
#define ABOX_CAN_ERROR_BUS_OFF       0x00000004U
#define ABOX_CAN_ERROR_STUFF         0x00000008U
#define ABOX_CAN_ERROR_FORM          0x00000010U
#define ABOX_CAN_ERROR_ACK           0x00000020U
#define ABOX_CAN_ERROR_BIT_RECESSIVE 0x00000040U
#define ABOX_CAN_ERROR_BIT_DOMINANT  0x00000080U
#define ABOX_CAN_ERROR_CRC           0x00000100U
#define ABOX_CAN_ERROR_TX_ARBITRATION_0 0x00000800U
#define ABOX_CAN_ERROR_TX_FAILURE_0    0x00001000U

typedef enum {
    ABOX_CAN_TX_SUCCESS,
    ABOX_CAN_TX_ARBITRATION_LOST,
    ABOX_CAN_TX_ERROR,
    ABOX_CAN_TX_ABORTED
} ABoxCanTxResult;

typedef struct {
    void *context;
    int (*snapshot)(void *context, ABoxCanSnapshot *snapshot);
    void (*clear_tx)(void *context, uint8_t mailbox);
    int (*send_mailbox)(void *context, uint8_t mailbox, const ABoxCanFrame *frame);
    void (*abort_mask)(void *context, uint8_t mailbox_mask);
    int (*receive)(void *context, ABoxCanFrame *frame);
    void (*clear_error)(void *context);
} ABoxCanEventHardware;

typedef struct {
    void *context;
    /* Called before clearing results or reusing the mailbox. */
    void (*observe)(void *context, const ABoxCanSnapshot *snapshot, uint8_t mailbox_mask);
    void (*complete)(void *context, uint8_t mailbox, ABoxCanTxResult result);
    void (*receive)(void *context, const ABoxCanFrame *frame);
    void (*error)(void *context, const ABoxCanSnapshot *snapshot, uint32_t error_bits);
    void (*irq_snapshot)(void *context, const ABoxCanSnapshot *snapshot);
    void (*irq_done)(void *context);
} ABoxCanEventCallbacks;

typedef struct {
    ABoxCanEventHardware hardware;
    ABoxCanEventCallbacks callbacks;
} ABoxCanEvents;

/* All operations for one controller, including interrupt handling, must be
 * serialized by the owner. Callbacks run in the caller's context. */
int ABoxCanEvents_Init(ABoxCanEvents *events, const ABoxCanEventHardware *hardware,
                       const ABoxCanEventCallbacks *callbacks);
int ABoxCanEvents_Reap(ABoxCanEvents *events);
int ABoxCanEvents_Read(ABoxCanEvents *events, ABoxCanSnapshot *snapshot);
/* Dispatch one controller IRQ while holding the owner's ISR-safe lock. */
int ABoxCanEvents_Irq(ABoxCanEvents *events);
/* Returns 1 queued, 0 when busy, -1 on invalid input or hardware failure.
 * The output mailbox is only updated on success. Prior results for a reused
 * mailbox are reported before it can be assigned to the new frame. */
int ABoxCanEvents_Send(ABoxCanEvents *events, const ABoxCanFrame *frame, uint8_t *mailbox);
int ABoxCanEvents_Abort(ABoxCanEvents *events, uint8_t mailbox_mask);

/* Bind an initialized controller to the event API. Once bound, do not use
 * ABoxCanPort_Send/SendTracked for that controller until it is stopped. */
int ABoxCanPort_BindEvents(unsigned bus, ABoxCanEvents *events,
                           const ABoxCanEventCallbacks *callbacks);
/* IRQ dispatch must run under an ISR-safe owner lock. The caller configures
 * NVIC priority at or below the scheduler's maximum syscall priority. */
int ABoxCanPort_EnableEventIrq(unsigned bus, ABoxCanEvents *events);
void ABoxCanPort_DisableEventIrq(unsigned bus);
void ABoxCanPort_DispatchEventIrq(unsigned bus);

#endif
