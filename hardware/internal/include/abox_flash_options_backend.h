#ifndef ABOX_FLASH_OPTIONS_BACKEND_H
#define ABOX_FLASH_OPTIONS_BACKEND_H
#include "abox_flash_options.h"
ABoxHalStatus ABoxFlashOptionsHw_ReadActive(ABoxFlashOptions *options);
ABoxHalStatus ABoxFlashOptionsHw_Write(const ABoxFlashOptions *options);
#endif
