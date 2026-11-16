#ifndef ABOX_CAN_PORT_H
#define ABOX_CAN_PORT_H
#include <stdint.h>

typedef struct {
    uint32_t id;
    uint8_t length, extended, remote;
    uint8_t data[8];
} ABoxCanFrame;
typedef enum {
    ABOX_CAN_NORMAL, ABOX_CAN_LOOPBACK, ABOX_CAN_SILENT, ABOX_CAN_SILENT_LOOPBACK
} ABoxCanMode;
typedef struct {
    void *context;
    int (*prepare)(void *context,unsigned bus,ABoxCanMode mode);
    void (*release)(void *context,unsigned bus,ABoxCanMode mode);
} ABoxCanBoardOps;
typedef struct {
    uint8_t auto_retransmit;
    uint8_t auto_bus_off_recovery;
    uint8_t rx_fifo_overwrite;
} ABoxCanOptions;

/* Disabled accepts all frames. When enabled, match the selected identifier
 * format and (id & mask), optionally distinguishing data and remote frames. */
typedef struct {
    uint32_t id, mask;
    uint8_t enabled, extended, match_remote, remote;
} ABoxCanFilter;

/* Time segments and SJW are expressed in time quanta, not register values.
 * The clock must divide bitrate * (1 + segment1 + segment2) exactly. */
typedef struct {
    uint32_t bitrate;
    uint8_t segment1;
    uint8_t segment2;
    uint8_t sync_jump_width;
    ABoxCanMode mode;
    ABoxCanOptions options;
    ABoxCanFilter filter;
} ABoxCanConfig;

/* Bind optional board pin/transceiver operations while both buses are stopped.
 * NULL selects no board operations. The callback table is copied; its context
 * remains valid until the next bind. Callbacks must not reenter CAN and must
 * be nonblocking, including when Suspend is called from a critical section.
 * A prepare callback must undo its own partial changes before returning failure.
 * The board, rather than the controller backend, decides pinmux, IRQ and
 * transceiver standby. A custom STM32 board also supplies its own HAL MSP. */
int ABoxCanPort_BindBoard(const ABoxCanBoardOps *ops);
/* bus 0/1 identifies the board's first/second CAN controller. Init owns the
 * controller and FIFO0 until Stop; do not use a vendor IRQ consumer at the
 * same time. Calls must be serialized by the owner. Bitrate must be
 * representable by the current APB1 clock. Stop bus 1 before bus 0. */
/* No product policy is implicit: options must be 0 or 1, segment1 1..16,
 * segment2 1..8, SJW 1..4 and no greater than segment2. Invalid or
 * unrepresentable configurations fail without initializing the bus. */
int ABoxCanPort_Init(unsigned bus, const ABoxCanConfig *config);
int ABoxCanPort_Stop(unsigned bus);
int ABoxCanPort_IsReady(unsigned bus);
/* Immediately quiesce a controller without waiting for a scheduler tick.
 * This can be used from a serialized power-off critical section. Pending
 * frames are discarded; Init is required before use again. Stop bus 1
 * before bus 0. Board transceiver power/standby remains the board's duty. */
int ABoxCanPort_Suspend(unsigned bus);
/* Send returns 1 when queued, 0 if all mailboxes are busy, -1 on error.
 * Queue acceptance alone does not prove bus transmission or peer receipt. */
int ABoxCanPort_Send(unsigned bus, const ABoxCanFrame *frame);
/* Like Send, but reports the selected hardware mailbox (0..2) on success.
 * The mailbox is an ownership token, not confirmation of transmission. The
 * caller must service any prior completion before reusing a free mailbox. */
int ABoxCanPort_SendTracked(unsigned bus, const ABoxCanFrame *frame, uint8_t *mailbox);
/* Receive returns 1 for a frame, 0 for an empty FIFO, -1 on error. */
int ABoxCanPort_Receive(unsigned bus, ABoxCanFrame *frame);
/* Each controller owns 14 filter slots (0..13). Slot 0 is initialized from
 * ABoxCanConfig.filter and initially routes to FIFO0; slots 1..13 are disabled
 * at each Init. A NULL filter disables a slot. A non-NULL filter with enabled=0
 * accepts all frames, as in the initial configuration. FIFO is 0 or 1.
 * Filter RAM is shared by both controllers: serialize configuration across
 * both buses. Hardware enters filter initialization briefly and frames may be
 * lost on either bus, so coordinate changes with both owners. */
int ABoxCanPort_SetFilter(unsigned bus,unsigned slot,const ABoxCanFilter *filter,unsigned fifo);
/* FIFO0 is also used by the event service; do not poll it while bound.
 * FIFO1 may be polled or delivered to its optional event callback, not both.
 * FIFO storage and filtering do not guarantee lossless reception. */
int ABoxCanPort_ReceiveFifo(unsigned bus,unsigned fifo,ABoxCanFrame *frame);
#endif
