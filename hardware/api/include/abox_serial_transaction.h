#ifndef ABOX_SERIAL_TRANSACTION_H
#define ABOX_SERIAL_TRANSACTION_H
#include <stdint.h>

/* One product serial port, one transaction owner. Receive is armed before
 * transmit so a fast response or local echo is retained. A bounded queue
 * preserves idle-delimited chunks; overflow or UART errors fail the request.
 * Poll and cancellation belong to one task; callbacks execute in the ISR. */
#define ABOX_SERIAL_TRANSACTION_CHUNK_SIZE 64U
#define ABOX_SERIAL_TRANSACTION_SLOTS 4U
typedef struct {
    uint8_t rx[ABOX_SERIAL_TRANSACTION_CHUNK_SIZE];
    uint8_t chunks[ABOX_SERIAL_TRANSACTION_SLOTS][ABOX_SERIAL_TRANSACTION_CHUNK_SIZE];
    uint16_t sizes[ABOX_SERIAL_TRANSACTION_SLOTS];
    uint8_t head, tail, error;
} ABoxSerialTransaction;

/* Bind this instance before use; replaces the serial port event callbacks. */
void ABoxSerialTransaction_Init(ABoxSerialTransaction *transaction);
int ABoxSerialTransaction_Begin(ABoxSerialTransaction *transaction,
                                const uint8_t *request, uint16_t size, uint32_t timeout_ms);
/* 1 = chunk copied, 0 = no chunk yet, -1 = transport error/overflow or too
 * small a destination. No chunk is consumed on an undersized destination. */
int ABoxSerialTransaction_Take(ABoxSerialTransaction *transaction,
                               uint8_t *destination, uint16_t capacity, uint16_t *length);
int ABoxSerialTransaction_End(ABoxSerialTransaction *transaction);
#endif
