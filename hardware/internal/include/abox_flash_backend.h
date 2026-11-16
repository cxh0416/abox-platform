#ifndef ABOX_FLASH_BACKEND_H
#define ABOX_FLASH_BACKEND_H
#include "abox_flash.h"
const ABoxFlashGeometry *ABoxFlashHw_Geometry(void);
int ABoxFlashHw_Begin(void);
int ABoxFlashHw_Program(uint32_t address, const uint8_t *data, uint32_t length);
int ABoxFlashHw_ErasePage(uint32_t address);
int ABoxFlashHw_End(void);
int ABoxFlashHw_Read(uint32_t address, uint8_t *data, uint32_t length);
#endif
