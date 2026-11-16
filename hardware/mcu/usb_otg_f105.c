#include "abox_usb_otg_backend.h"
#if defined(ABOX_MCU_GD32F105)
#include "gd32f10x.h"
#define ABOX_OTG_BASE USBFS_BASE
#else
#include "stm32f1xx.h"
#define ABOX_OTG_BASE USB_OTG_FS_PERIPH_BASE
#endif
ABoxUsbOtgRegisters *ABoxUsbOtgHw_Registers(void)
{ return (ABoxUsbOtgRegisters *)(uintptr_t)ABOX_OTG_BASE; }
