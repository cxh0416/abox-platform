#include <assert.h>
#include <string.h>
#include "abox_serial_transaction.h"
#include "abox_serial_port.h"

static const ABoxSerialPortEvents *events;
static uint8_t *rx;
static uint16_t rx_capacity;
static int active, send_ok = 1, flushes;
void ABoxSerialPort_SetEvents(const ABoxSerialPortEvents *value) { events = value; }
int ABoxSerialPort_AbortRx(void) { active = 0; return 1; }
void ABoxSerialPort_FlushRx(void) { assert(!active); ++flushes; }
int ABoxSerialPort_StartRx(uint8_t *buffer, uint16_t size)
{ assert(!active); rx = buffer; rx_capacity = size; active = 1; return 1; }
int ABoxSerialPort_SendBlocking(const uint8_t *data, uint16_t size, uint32_t timeout)
{ assert(active && data && size && timeout); return send_ok; }
static void deliver(const char *data)
{
    size_t size = strlen(data);
    assert(active && size <= rx_capacity);
    memcpy(rx, data, size);
    active = 0;
    events->rx(rx, (uint16_t)size);
    assert(active);
}
int main(void)
{
    ABoxSerialTransaction t;
    uint8_t out[8]; uint16_t length;
    const uint8_t request[] = {1U};
    ABoxSerialTransaction_Init(&t);
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
    events->error();
    assert(ABoxSerialTransaction_Take(&t, out, sizeof(out), &length) == -1);
    assert(ABoxSerialTransaction_End(&t));
    send_ok = 0;
    assert(!ABoxSerialTransaction_Begin(&t, request, 1U, 20U) && !active);
    return 0;
}
