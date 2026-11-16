#include <assert.h>
#include <string.h>
#include "abox_serial_transaction.h"
#include "abox_uart.h"

static ABoxUartEvents events;
static uint8_t *rx;
static uint16_t rx_capacity;
static int active, send_ok = 1, flushes;
uint32_t ABoxUart_Capabilities(ABoxUartId id)
{ return id == 7U ? ABOX_UART_CAP_RX_IDLE | ABOX_UART_CAP_FLUSH_RX | ABOX_UART_CAP_BLOCKING_TX : 0U; }
int ABoxUart_SetEvents(ABoxUartId id, const ABoxUartEvents *value) { assert(id == 7U); events = *value; return 1; }
int ABoxUart_AbortRx(ABoxUartId id) { assert(id == 7U); active = 0; return 1; }
int ABoxUart_FlushRx(ABoxUartId id) { assert(id == 7U && !active); ++flushes; return 1; }
int ABoxUart_StartRx(ABoxUartId id, ABoxUartRxMode mode, uint8_t *buffer, uint16_t size)
{ assert(id == 7U && mode == ABOX_UART_RX_IDLE && !active); rx = buffer; rx_capacity = size; active = 1; return 1; }
int ABoxUart_SendBlocking(ABoxUartId id, const uint8_t *data, uint16_t size, uint32_t timeout)
{ assert(id == 7U && active && data && size && timeout); return send_ok; }
static void deliver(const char *data)
{
    size_t size = strlen(data);
    assert(active && size <= rx_capacity);
    memcpy(rx, data, size);
    active = 0;
    events.rx(events.context, rx, (uint16_t)size);
    assert(active);
}
int main(void)
{
    ABoxSerialTransaction t;
    uint8_t out[8]; uint16_t length;
    const uint8_t request[] = {1U};
    assert(!ABoxSerialTransaction_Init(&t, 8U));
    assert(ABoxSerialTransaction_Init(&t, 7U));
    assert(ABoxSerialTransaction_Begin(&t, request, 1U, 20U) && flushes == 1);
    deliver("echo"); deliver("reply");
    assert(ABoxSerialTransaction_Take(&t, out, 3U, &length) == -1);
    assert(ABoxSerialTransaction_Take(&t, out, sizeof(out), &length) == 1);
    assert(length == 4U && !memcmp(out, "echo", length));
    assert(ABoxSerialTransaction_Take(&t, out, sizeof(out), &length) == 1);
    assert(length == 5U && !memcmp(out, "reply", length));
    assert(ABoxSerialTransaction_Take(&t, out, sizeof(out), &length) == 0);
    deliver("1"); deliver("2"); deliver("3"); deliver("4");
    assert(ABoxSerialTransaction_Take(&t, out, sizeof(out), &length) == -1);
    assert(ABoxSerialTransaction_End(&t) && !active);
    assert(ABoxSerialTransaction_Begin(&t, request, 1U, 20U));
    events.error(events.context);
    assert(ABoxSerialTransaction_Take(&t, out, sizeof(out), &length) == -1);
    assert(ABoxSerialTransaction_End(&t));
    send_ok = 0;
    assert(!ABoxSerialTransaction_Begin(&t, request, 1U, 20U) && !active);
    return 0;
}
