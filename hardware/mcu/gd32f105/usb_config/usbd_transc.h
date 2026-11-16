#ifndef ABOX_GD_USBD_TRANSC_H
#define ABOX_GD_USBD_TRANSC_H
#include "drv_usb_core.h"
enum { USBD_DEFAULT, USBD_CONFIGURED, USBD_SUSPENDED };
uint8_t usbd_setup_transc(usb_core_driver *udev);
uint8_t usbd_out_transc(usb_core_driver *udev,uint8_t endpoint);
uint8_t usbd_in_transc(usb_core_driver *udev,uint8_t endpoint);
#endif
