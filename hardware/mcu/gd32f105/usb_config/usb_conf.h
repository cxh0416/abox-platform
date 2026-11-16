#ifndef ABOX_GD_USB_CONF_H
#define ABOX_GD_USB_CONF_H
#include <stdint.h>
#include <stddef.h>
#include "gd32f10x.h"
#include "gd32f10x_pmu.h"
#define USE_DEVICE_MODE
#define USE_HOST_MODE
#define VBUS_SENSING_ENABLED
#define USB_SOF_OUTPUT 0U
#define USB_LOW_POWER 0U
#define RX_FIFO_FS_SIZE 128U
#define TX0_FIFO_FS_SIZE 64U
#define TX1_FIFO_FS_SIZE 64U
#define TX2_FIFO_FS_SIZE 32U
#define TX3_FIFO_FS_SIZE 32U
#define USB_RX_FIFO_FS_SIZE 128U
#define USB_HTX_NPFIFO_FS_SIZE 96U
#define USB_HTX_PFIFO_FS_SIZE 96U
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#endif
