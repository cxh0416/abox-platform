#ifndef ABOX_SERIAL_PORT_H
#define ABOX_SERIAL_PORT_H

#include <stdint.h>

typedef struct {
    void (*rx)(const uint8_t *data, uint16_t length);
    void (*tx_complete)(void);
    void (*error)(void);
} ABoxSerialPortEvents;

/* Bind before reception starts. Callbacks execute in interrupt context. */
void ABoxSerialPort_SetEvents(const ABoxSerialPortEvents *events);
void ABoxSerialPort_NotifyRx(const uint8_t *data, uint16_t length);
void ABoxSerialPort_NotifyTxComplete(void);
void ABoxSerialPort_NotifyError(void);

/* Product serial bus hardware interface. The selected board target supplies
 * one implementation. All operations return nonzero on success. RX storage
 * remains owned by the caller; async TX storage must remain valid until its
 * completion or cancellation callback. The board routes RX, TX complete and
 * errors to the product's existing callbacks. */
int ABoxSerialPort_IsConfig(uint32_t baud);
/* Stop active transfers before deinit/reconfiguration. SetConfig selects
 * 8N1; Init reports failure for an unsupported configuration. */
int ABoxSerialPort_Deinit(void);
void ABoxSerialPort_SetConfig(uint32_t baud);
int ABoxSerialPort_Init(void);
int ABoxSerialPort_StartRx(uint8_t *data, uint16_t length);
int ABoxSerialPort_AbortRx(void);
/* Call only after aborting RX; discard a stale hardware receive byte/status. */
void ABoxSerialPort_FlushRx(void);
int ABoxSerialPort_Send(const uint8_t *data, uint16_t length);
void ABoxSerialPort_AbortTx(void);
int ABoxSerialPort_SendBlocking(const uint8_t *data, uint16_t length, uint32_t timeout_ms);

#endif
