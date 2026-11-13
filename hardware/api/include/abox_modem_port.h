#ifndef ABOX_MODEM_PORT_H
#define ABOX_MODEM_PORT_H
#include <stdint.h>

/* Circular DMA reports positions in [0, capacity]; capacity denotes a full
 * wrap. The callback runs in IRQ context. RX buffer remains caller-owned. */
typedef struct {
    void (*rx_position)(uint16_t position);
    void (*error)(void);
} ABoxModemPortEvents;
void ABoxModemPort_SetEvents(const ABoxModemPortEvents *events);
void ABoxModemPort_NotifyRx(uint16_t position);
void ABoxModemPort_NotifyError(void);
int ABoxModemPort_AbortRx(void);
int ABoxModemPort_StartRx(uint8_t *buffer, uint16_t capacity);
int ABoxModemPort_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms);
#endif
