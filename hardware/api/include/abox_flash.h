#ifndef ABOX_FLASH_H
#define ABOX_FLASH_H
#include <stdint.h>
#include "abox_hal_status.h"

typedef struct { uint32_t base, size, erase_size, program_unit, program_max_size; } ABoxFlashGeometry;
/* Physical MCU flash, not an allocation or permission policy. The caller must
 * reserve its own partition, serialize all access, and avoid erasing Boot,
 * state, configuration and other owners' pages. Writes/erases are not atomic
 * across units; after failure any prefix may have changed. */
ABoxHalStatus ABoxFlash_Geometry(ABoxFlashGeometry *geometry);
ABoxHalStatus ABoxFlash_Begin(void);
ABoxHalStatus ABoxFlash_Program(uint32_t address, const uint8_t *data, uint32_t length);
ABoxHalStatus ABoxFlash_ErasePage(uint32_t address);
ABoxHalStatus ABoxFlash_End(void);
ABoxHalStatus ABoxFlash_Read(uint32_t address, uint8_t *data, uint32_t length);
#endif
