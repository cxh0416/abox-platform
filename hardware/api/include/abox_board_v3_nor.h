#ifndef ABOX_BOARD_V3_NOR_H
#define ABOX_BOARD_V3_NOR_H
#include "abox_nor.h"

/* Owns SPI1 and PA4..PA7. Call after clocks and the supplied tick are live. */
int ABoxBoardV3_NorOpen(ABoxNor *nor, uint32_t (*tick_ms)(void *), uint32_t *jedec_id);
#endif
