#include "abox_serial_port.h"

static ABoxSerialPortEvents g_events;

void ABoxSerialPort_SetEvents(const ABoxSerialPortEvents *events)
{
    if (events) g_events = *events;
    else g_events = (ABoxSerialPortEvents){0};
}
void ABoxSerialPort_NotifyRx(const uint8_t *data, uint16_t length)
{ if (g_events.rx) g_events.rx(data, length); }
void ABoxSerialPort_NotifyTxComplete(void)
{ if (g_events.tx_complete) g_events.tx_complete(); }
void ABoxSerialPort_NotifyError(void)
{ if (g_events.error) g_events.error(); }
