#include "abox_modem_port.h"
static ABoxModemPortEvents g_events;
void ABoxModemPort_SetEvents(const ABoxModemPortEvents *events)
{ if (events) g_events = *events; else g_events = (ABoxModemPortEvents){0}; }
void ABoxModemPort_NotifyRx(uint16_t position)
{ if (g_events.rx_position) g_events.rx_position(position); }
void ABoxModemPort_NotifyError(void)
{ if (g_events.error) g_events.error(); }
