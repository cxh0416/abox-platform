#ifndef ABOX_BOARD_H
#define ABOX_BOARD_H

#include <stdint.h>

/* Initialize the selected MCU and board and bind platform services. The
 * caller supplies the App vector address and stops startup on failure. */
int ABoxBoard_Init(uint32_t vector_address);

/* Optional diagnostic hooks. UART event delivery remains owned by the port. */
void ABoxBoard_ModemUartError(uint32_t error, uint32_t state);
void ABoxBoard_SerialUartError(uint32_t error);

#endif
