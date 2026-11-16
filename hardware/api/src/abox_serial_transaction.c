#include "abox_serial_transaction.h"
#include "abox_uart.h"
#include <string.h>

static void on_rx(void *context, const uint8_t *data, uint16_t length)
{
    ABoxSerialTransaction *t = context;
    uint8_t head, next;
    if (!t) return;
    head = __atomic_load_n(&t->head, __ATOMIC_RELAXED);
    next = (uint8_t)((head + 1U) % ABOX_SERIAL_TRANSACTION_SLOTS);
    if (length > ABOX_SERIAL_TRANSACTION_CHUNK_SIZE ||
        next == __atomic_load_n(&t->tail, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&t->error, 1U, __ATOMIC_RELEASE);
    } else {
        memcpy(t->chunks[head], data, length);
        t->sizes[head] = length;
        __atomic_store_n(&t->head, next, __ATOMIC_RELEASE);
    }
    if (!ABoxUart_StartRx(t->uart, ABOX_UART_RX_IDLE, t->rx, sizeof(t->rx)))
        __atomic_store_n(&t->error, 1U, __ATOMIC_RELEASE);
}
static void on_error(void *context)
{ ABoxSerialTransaction *t = context; if (t) __atomic_store_n(&t->error, 1U, __ATOMIC_RELEASE); }

int ABoxSerialTransaction_Init(ABoxSerialTransaction *transaction, ABoxUartId uart)
{
    const uint32_t required = ABOX_UART_CAP_RX_IDLE | ABOX_UART_CAP_FLUSH_RX | ABOX_UART_CAP_BLOCKING_TX;
    if (!transaction || (ABoxUart_Capabilities(uart) & required) != required) return 0;
    memset(transaction, 0, sizeof(*transaction));
    transaction->uart = uart;
    transaction->events = (ABoxUartEvents){transaction, on_rx, 0, on_error, 0};
    return ABoxUart_SetEvents(uart, &transaction->events);
}
int ABoxSerialTransaction_Begin(ABoxSerialTransaction *t, const uint8_t *request,
                                uint16_t size, uint32_t timeout_ms)
{
    if (!t || !request || !size || !ABoxUart_AbortRx(t->uart) || !ABoxUart_FlushRx(t->uart)) return 0;
    __atomic_store_n(&t->head, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&t->tail, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&t->error, 0U, __ATOMIC_RELEASE);
    if (!ABoxUart_StartRx(t->uart, ABOX_UART_RX_IDLE, t->rx, sizeof(t->rx))) return 0;
    if (ABoxUart_SendBlocking(t->uart, request, size, timeout_ms)) return 1;
    (void)ABoxUart_AbortRx(t->uart);
    return 0;
}
int ABoxSerialTransaction_Take(ABoxSerialTransaction *t, uint8_t *destination,
                               uint16_t capacity, uint16_t *length)
{
    uint8_t tail;
    uint16_t size;
    if (!t || !destination || !length) return -1;
    *length = 0U;
    if (__atomic_load_n(&t->error, __ATOMIC_ACQUIRE)) return -1;
    tail = __atomic_load_n(&t->tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&t->head, __ATOMIC_ACQUIRE)) return 0;
    size = t->sizes[tail];
    if (size > capacity) return -1;
    memcpy(destination, t->chunks[tail], size);
    *length = size;
    __atomic_store_n(&t->tail, (uint8_t)((tail + 1U) % ABOX_SERIAL_TRANSACTION_SLOTS), __ATOMIC_RELEASE);
    return 1;
}
int ABoxSerialTransaction_End(ABoxSerialTransaction *t)
{ return t && ABoxUart_AbortRx(t->uart); }
