#ifndef ABOX_CAN_PORT_H
#define ABOX_CAN_PORT_H
#include <stdint.h>

typedef struct {
    uint32_t id;
    uint8_t length, extended, remote;
    uint8_t data[8];
} ABoxCanFrame;
typedef enum { ABOX_CAN_NORMAL = 0, ABOX_CAN_SILENT_LOOPBACK = 1 } ABoxCanMode;
typedef struct {
    uint8_t auto_retransmit;
    uint8_t auto_bus_off_recovery;
    uint8_t rx_fifo_overwrite;
} ABoxCanOptions;

/* bus 0/1 identifies the board's first/second CAN controller. Init owns the
 * controller and FIFO0 until Stop; do not use a vendor IRQ consumer at the
 * same time. Calls must be serialized by the owner. Bitrate must be
 * representable by the current APB1 clock. Stop bus 1 before bus 0. */
int ABoxCanPort_Init(unsigned bus, uint32_t bitrate, ABoxCanMode mode);
/* Explicit bus policy; all fields must be 0 or 1. Init retains each MCU's
 * historical FIFO behavior and defaults to retransmit on, recovery off. */
int ABoxCanPort_InitWithOptions(unsigned bus, uint32_t bitrate, ABoxCanMode mode,
                                const ABoxCanOptions *options);
int ABoxCanPort_Stop(unsigned bus);
int ABoxCanPort_IsReady(unsigned bus);
/* Send returns 1 when queued, 0 if all mailboxes are busy, -1 on error.
 * Queue acceptance alone does not prove bus transmission or peer receipt. */
int ABoxCanPort_Send(unsigned bus, const ABoxCanFrame *frame);
/* Like Send, but reports the selected hardware mailbox (0..2) on success.
 * The mailbox is an ownership token, not confirmation of transmission. The
 * caller must service any prior completion before reusing a free mailbox. */
int ABoxCanPort_SendTracked(unsigned bus, const ABoxCanFrame *frame, uint8_t *mailbox);
/* Receive returns 1 for a frame, 0 for an empty FIFO, -1 on error. */
int ABoxCanPort_Receive(unsigned bus, ABoxCanFrame *frame);
#endif
