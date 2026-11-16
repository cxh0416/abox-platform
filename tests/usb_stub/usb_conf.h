#ifndef ABOX_TEST_USB_CONF_H
#define ABOX_TEST_USB_CONF_H
#include <stddef.h>
#include <stdint.h>
#define __IO volatile
#define __STATIC_INLINE static inline
#define __packed __attribute__((__packed__))
#define BIT(n) (UINT32_C(1) << (n))
#define BITS(lo,hi) ((UINT32_MAX >> (31U - ((hi) - (lo)))) << (lo))
#define USE_DEVICE_MODE
#define USE_HOST_MODE
#define USB_SOF_OUTPUT 0U
#define USB_LOW_POWER 0U
#endif
