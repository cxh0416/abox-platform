#ifndef ABOX_GD32F105_FLASH_H
#define ABOX_GD32F105_FLASH_H

#include <stdint.h>

/* ABoxPlatformPort-compatible callbacks for a GD32F105RCT6 (256 KiB).
 * A successful write or erase reports controller completion only; callers
 * remain responsible for their own address ownership and data readback. */
int ABoxGd32Flash_Begin(void *context);
int ABoxGd32Flash_Write(void *context, uint32_t address, const uint8_t *data, uint32_t length);
int ABoxGd32Flash_ErasePage(void *context, uint32_t address);
void ABoxGd32Flash_End(void *context);

#endif
