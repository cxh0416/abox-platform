#ifndef ABOX_BOARD_H
#define ABOX_BOARD_H

#include <stdint.h>
#include "abox_uart.h"
#include "abox_mcu_clock.h"

#define ABOX_BOARD_UART_BUS 5U
#define ABOX_BOARD_UART_MODEM 1U
#define ABOX_BOARD_UART_RS232 3U
#define ABOX_BOARD_UART_TTL   4U
#define ABOX_BOARD_UART_RS485 5U

typedef struct {
    ABoxUartId id;
    ABoxUartConfig config;
} ABoxBoardUartConfig;
typedef struct {
    uint32_t vector_address;
    const ABoxBoardUartConfig *uarts;
    unsigned uart_count;
    const ABoxMcuClockConfig *clock;
} ABoxBoardConfig;

/* Initialize the selected MCU and board and bind platform services. The
 * caller supplies the App vector address and clock configuration and stops
 * startup on failure. */
int ABoxBoard_Init(const ABoxBoardConfig *config);

/* Configure the selected board's wired UART pins and IRQs. */
int ABoxBoardUart_Prepare(ABoxUartId id);

/* Optional diagnostic hooks. UART event delivery remains owned by the port. */
void ABoxBoard_ModemUartError(uint32_t error, uint32_t state);
void ABoxBoard_SerialUartError(uint32_t error);

#endif
