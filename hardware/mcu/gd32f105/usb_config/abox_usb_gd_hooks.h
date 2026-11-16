#ifndef ABOX_USB_GD_HOOKS_H
#define ABOX_USB_GD_HOOKS_H
#include "drv_usb_core.h"
void ABoxUsbGd_Discard(usb_core_driver *udev,uint32_t count);
void ABoxUsbGd_ReceiveError(unsigned host,unsigned number);
void ABoxUsbGd_DeviceSession(unsigned connected);
#endif
