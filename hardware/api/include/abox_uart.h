#ifndef ABOX_UART_H
#define ABOX_UART_H
#include <stdint.h>

/* Physical UARTs 1..5: STM32 USART1/2/3, UART4/5 and GD32
 * USART0/1/2, UART3/4 respectively. GPIO, remap and NVIC belong to the board. */
typedef unsigned ABoxUartId;
typedef enum { ABOX_UART_PARITY_NONE, ABOX_UART_PARITY_EVEN, ABOX_UART_PARITY_ODD } ABoxUartParity;
typedef enum { ABOX_UART_FLOW_NONE, ABOX_UART_FLOW_RTS, ABOX_UART_FLOW_CTS,
               ABOX_UART_FLOW_RTS_CTS } ABoxUartFlow;
typedef enum { ABOX_UART_ASYNC, ABOX_UART_SYNC_MASTER, ABOX_UART_LIN,
               ABOX_UART_HALF_DUPLEX, ABOX_UART_IRDA, ABOX_UART_SMARTCARD,
               ABOX_UART_MULTIPROCESSOR } ABoxUartMode;
#define ABOX_UART_STOP_1      1U
#define ABOX_UART_STOP_2      2U
#define ABOX_UART_STOP_HALF   3U
#define ABOX_UART_STOP_1_HALF 4U
typedef struct {
    uint32_t baud;
    uint8_t data_bits; /* 7 with parity, 8 with/without parity, or 9 without parity */
    uint8_t stop_bits;
    ABoxUartParity parity;
    ABoxUartFlow flow;
    ABoxUartMode mode;
    union {
        struct { uint8_t polarity, phase, last_bit_clock; } sync;
        struct { uint8_t break_bits; } lin; /* 10 or 11 bit detection */
        struct { uint8_t low_power, prescaler; } irda;
        struct { uint8_t clock_output, prescaler, guard_time, nack; } smartcard;
        struct { uint8_t address, wake_on_address; } multiprocessor; /* 4-bit address */
    } options;
} ABoxUartConfig;
typedef enum { ABOX_UART_RX_IDLE, ABOX_UART_RX_CIRCULAR_DMA,
               ABOX_UART_RX_FIXED, ABOX_UART_RX_DMA, ABOX_UART_RX_IDLE_DMA } ABoxUartRxMode;
#define ABOX_UART_CAP_RX_IDLE          (1UL << 0)
#define ABOX_UART_CAP_RX_CIRCULAR_DMA  (1UL << 1)
#define ABOX_UART_CAP_ASYNC_TX         (1UL << 2)
#define ABOX_UART_CAP_BLOCKING_TX      (1UL << 3)
#define ABOX_UART_CAP_FLUSH_RX         (1UL << 4)
#define ABOX_UART_CAP_RX_FIXED         (1UL << 5)
#define ABOX_UART_CAP_RX_DMA           (1UL << 6)
#define ABOX_UART_CAP_TX_DMA           (1UL << 7)
#define ABOX_UART_CAP_SYNC_MASTER      (1UL << 8)
#define ABOX_UART_CAP_LIN              (1UL << 9)
#define ABOX_UART_CAP_HALF_DUPLEX      (1UL << 10)
#define ABOX_UART_CAP_IRDA             (1UL << 11)
#define ABOX_UART_CAP_SMARTCARD        (1UL << 12)
#define ABOX_UART_CAP_BLOCKING_RX      (1UL << 13)
#define ABOX_UART_CAP_MULTIPROCESSOR   (1UL << 14)
#define ABOX_UART_ERROR_PARITY  (1U << 0)
#define ABOX_UART_ERROR_FRAMING (1U << 1)
#define ABOX_UART_ERROR_NOISE   (1U << 2)
#define ABOX_UART_ERROR_OVERRUN (1U << 3)
#define ABOX_UART_ERROR_RX_DMA  (1U << 4)
#define ABOX_UART_ERROR_TX_DMA  (1U << 5)
#define ABOX_UART_ERROR_RX_LOST (1U << 6)
typedef struct {
    void *context;
    /* One-shot: size is the number of completed frames. Circular DMA: size
     * is the write position in [0, capacity]; capacity denotes wrap. */
    void (*rx)(void *context, const uint8_t *data, uint16_t size);
    void (*tx_complete)(void *context);
    void (*error)(void *context);
    void (*lin_break)(void *context);
} ABoxUartEvents;

/* An optional ABOX_UART_ENABLED_MASK build definition reserves memory only for
 * selected physical instances; bit 0 denotes UART1. Default enables all five.
 * Callbacks run in IRQ context. A one-shot RX callback can immediately rearm;
 * a circular callback must not reconfigure the same transfer. The event table,
 * context and transfer storage remain owned by the caller until stopped.
 * Length counts frames. Nine-bit/no-parity data use aligned uint16_t elements;
 * all other data use uint8_t elements. DMA channels are reserved automatically;
 * UART5 has no DMA request on either F105. Seven-bit/parity receive uses
 * interrupt mode because the DMA byte also contains the received parity bit.
 * DMA TX completion is reported only
 * after the final frame has left the UART. A blocking timeout needs an
 * advancing platform tick. Synchronous mode supplies CK only while transmitting;
 * to receive, arm RX and transmit the required clocking frames. Half-duplex
 * and smartcard use one wire and do not permit overlapping RX and TX; the
 * board configures the open-drain pin and any external transceiver. LIN break
 * detection may also generate a framing error and stop an active receive;
 * rearm after the break callback. Smartcard protocol retries and IrDA turnaround
 * are application responsibilities. External board code dispatches the UART and DMA
 * interrupts and must not use the same peripherals or channels concurrently. */
uint32_t ABoxUart_Capabilities(ABoxUartId id);
int ABoxUart_Open(ABoxUartId id, const ABoxUartConfig *config);
int ABoxUart_Close(ABoxUartId id);
int ABoxUart_IsConfig(ABoxUartId id, const ABoxUartConfig *config);
/* Flags from the last error; blocking smartcard errors do not invoke callbacks. */
uint32_t ABoxUart_LastError(ABoxUartId id);
int ABoxUart_SetEvents(ABoxUartId id, const ABoxUartEvents *events);
int ABoxUart_StartRx(ABoxUartId id, ABoxUartRxMode mode, uint8_t *data, uint16_t length);
/* Blocking fixed-length receive reports the number of complete frames through
 * received even on timeout or receive error; partial data remains in data.
 * The caller must provide an advancing tick and serialize the instance. On
 * receive error LastError describes the discarded frame; no RX/error callback
 * is issued for this operation. TX may run concurrently except in one-wire
 * modes. A timeout does not clear bytes arriving after the call returns. */
int ABoxUart_ReceiveBlocking(ABoxUartId id,uint8_t *data,uint16_t length,uint32_t timeout_ms,uint16_t *received);
int ABoxUart_AbortRx(ABoxUartId id);
int ABoxUart_FlushRx(ABoxUartId id);
int ABoxUart_Send(ABoxUartId id, const uint8_t *data, uint16_t length);
int ABoxUart_SendDma(ABoxUartId id, const uint8_t *data, uint16_t length);
int ABoxUart_AbortTx(ABoxUartId id);
int ABoxUart_SendBlocking(ABoxUartId id, const uint8_t *data, uint16_t length, uint32_t timeout_ms);
/* LIN send requests a hardware break only when the transmitter is idle.
 * Pending reports the detection flag; Clear acknowledges it. A break callback
 * acknowledges the flag before invocation. */
int ABoxUart_LinSendBreak(ABoxUartId id);
int ABoxUart_LinBreakPending(ABoxUartId id, uint8_t *pending);
int ABoxUart_LinClearBreak(ABoxUartId id);
/* In multiprocessor mode, mute suppresses receive activity until an idle line
 * or a matching marked address, as configured at Open. SetMute(1) refuses a
 * pending unread frame. Hardware clears mute on wakeup; the protocol must
 * encode the address mark and handle the received address. */
int ABoxUart_SetMute(ABoxUartId id,uint8_t mute);
int ABoxUart_IsMuted(ABoxUartId id,uint8_t *mute);
void ABoxUart_Irq(ABoxUartId id);
/* Call for every UART-owned DMA channel on the actual DMA IRQ, including each
 * channel on a shared IRQ line. Other owners dispatch their own DMA handler. */
void ABoxUart_DmaIrq(unsigned controller, unsigned channel);
#endif
