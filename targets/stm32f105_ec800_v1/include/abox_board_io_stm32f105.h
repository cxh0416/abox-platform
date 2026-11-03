#ifndef ABOX_BOARD_IO_STM32F105_H
#define ABOX_BOARD_IO_STM32F105_H

#include "abox_board_io.h"

#define ABOX_BOARD_VERSION_V1 1
#define ABOX_BOARD_VERSION_V2 2
#ifndef ABOX_BOARD_VERSION
#define ABOX_BOARD_VERSION ABOX_BOARD_VERSION_V2
#endif
#if ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V1
#define ABOX_BOARD_VERSION_NAME "v1"
#define ABOX_BOARD_INPUTS_PRESENT 0x0fU
#elif ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V2
#define ABOX_BOARD_VERSION_NAME "v2"
#define ABOX_BOARD_INPUTS_PRESENT 0xffU
#else
#error "ABOX_BOARD_VERSION must select v1 or v2"
#endif

/* External connector positions, not STM32 port B pin numbers. */
typedef enum {
    ABOX_BOARD_TERMINAL_B4 = 4,
    ABOX_BOARD_TERMINAL_B5,
    ABOX_BOARD_TERMINAL_B6,
    ABOX_BOARD_TERMINAL_B7,
    ABOX_BOARD_TERMINAL_B8,
    ABOX_BOARD_TERMINAL_B9,
    ABOX_BOARD_TERMINAL_B10,
    ABOX_BOARD_TERMINAL_B11
} ABoxBoardTerminal;

/* Called by generated GPIO initialization before any board output is enabled. */
void ABoxBoardIoStm32_SafeInit(void);
/* Configure only the selected board's inputs; preserve outputs and LEDs. */
void ABoxBoardIoStm32_InputsInit(void);
/* Returns zero for a terminal without a digital input on the selected board.
 * A successful read is an instantaneous MCU level, without debounce. */
int ABoxBoardIoStm32_ReadTerminal(ABoxBoardTerminal terminal, uint8_t *level);
const ABoxBoardIoPort *ABoxBoardIoStm32_Port(void);

#endif
