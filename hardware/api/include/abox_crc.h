#ifndef ABOX_CRC_H
#define ABOX_CRC_H
#include <stdint.h>
#include "abox_hal_status.h"

/* The F105 has one CRC calculator, separate from SPI CRC. Reserve it against
 * all other owners, including direct vendor-library and DMA access. Calls on
 * the calculator must be serialized. Open resets the calculation; Close
 * releases it without resetting the independent 8-bit scratch register. */
ABoxHalStatus ABoxCrc_Open(void);
ABoxHalStatus ABoxCrc_Reset(void);
/* Each input is a 32-bit word processed most-significant bit first. F105 has
 * fixed polynomial 0x04c11db7, initial value 0xffffffff, no reflection and no
 * final XOR. A byte stream therefore needs an explicit word packing and padding
 * policy; this is not the reflected/zlib CRC-32 interface. The aligned input
 * can be split over calls without changing the result. */
ABoxHalStatus ABoxCrc_UpdateWords(const uint32_t *words,uint32_t count);
ABoxHalStatus ABoxCrc_Read(uint32_t *result);
/* The scratch register is not part of the CRC result. */
ABoxHalStatus ABoxCrc_WriteScratch(uint8_t value);
ABoxHalStatus ABoxCrc_ReadScratch(uint8_t *value);
ABoxHalStatus ABoxCrc_Close(void);
#endif
