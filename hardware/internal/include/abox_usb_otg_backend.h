#ifndef ABOX_USB_OTG_BACKEND_H
#define ABOX_USB_OTG_BACKEND_H
#include <stdint.h>
typedef struct {
    volatile uint32_t control,otg_interrupt,ahb_config,usb_config,reset,global_status,global_mask;
} ABoxUsbOtgRegisters;
ABoxUsbOtgRegisters *ABoxUsbOtgHw_Registers(void);
void ABoxUsbOtgCore_DisableKeepRole(uint8_t host);
int ABoxUsbOtgCore_IsAdopting(void);
#endif
