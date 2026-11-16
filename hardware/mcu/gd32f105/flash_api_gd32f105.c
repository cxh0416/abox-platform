#include <string.h>
#include "abox_flash_backend.h"
#include "abox_gd32f105_flash.h"

static const ABoxFlashGeometry geometry = {0x08000000U, 0x40000U, 2048U, 2U, 4U};
const ABoxFlashGeometry *ABoxFlashHw_Geometry(void) { return &geometry; }
int ABoxFlashHw_Begin(void) { return ABoxGd32Flash_Begin(0); }
int ABoxFlashHw_Program(uint32_t address, const uint8_t *data, uint32_t length)
{ return ABoxGd32Flash_Write(0, address, data, length); }
int ABoxFlashHw_ErasePage(uint32_t address) { return ABoxGd32Flash_ErasePage(0, address); }
int ABoxFlashHw_End(void) { ABoxGd32Flash_End(0); return 1; }
int ABoxFlashHw_Read(uint32_t address, uint8_t *data, uint32_t length)
{ memcpy(data, (const void *)(uintptr_t)address, length); return 1; }
