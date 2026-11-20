#ifndef ABOX_BOARD_V3_H
#define ABOX_BOARD_V3_H

#include "abox_board.h"

typedef enum {
    ABOX_BOARD_V3_SIM_PHYSICAL = 0,
    ABOX_BOARD_V3_SIM_ESIM = 1
} ABoxBoardV3Sim;

/* Call after Board_Init and while the modem/SIM supply is off and discharged.
 * The board rejects changes while its EC800 supply enable is asserted. */
int ABoxBoardV3_SelectSim(ABoxBoardV3Sim sim);
void ABoxBoardV3_SetBuzzer(uint8_t on);

/* UART5 must use asynchronous mode. This owns its TX and direction pins.
 * Serialize UART5 access and use this entry for transmission. */
int ABoxBoardV3_Rs485Send(const uint8_t *data, uint16_t length, uint32_t timeout_ms);

#endif
