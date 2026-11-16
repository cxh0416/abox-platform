#ifndef ABOX_USB_BACKEND_H
#define ABOX_USB_BACKEND_H
#include "abox_usb.h"
ABoxHalStatus ABoxUsbHw_Open(const ABoxUsbConfig *config);
ABoxHalStatus ABoxUsbHw_Start(void);
ABoxHalStatus ABoxUsbHw_Stop(void);
ABoxHalStatus ABoxUsbHw_Close(void);
void ABoxUsbHw_Irq(void);
void ABoxUsbHw_Poll(void);
#ifdef ABOX_USB_OTG_MANAGED
/* Called with controller interrupts masked, after hardware changes mode. */
void ABoxUsbHw_OtgPause(void);
void ABoxUsbHw_OtgSense(void);
void ABoxUsbHw_OtgResume(void);
void ABoxUsbHw_OtgAbort(void);
ABoxHalStatus ABoxUsbHw_OtgAdopt(const ABoxUsbConfig *config);
ABoxHalStatus ABoxUsbHw_OtgHostSignal(uint8_t suspend,uint8_t resume);
#endif
ABoxHalStatus ABoxUsbHw_DeviceOpen(const ABoxUsbDeviceEndpoint *endpoint);
ABoxHalStatus ABoxUsbHw_DeviceClose(uint8_t address);
ABoxHalStatus ABoxUsbHw_DeviceTransfer(uint8_t address,uint8_t *buffer,uint16_t length);
ABoxHalStatus ABoxUsbHw_DeviceCancel(uint8_t address);
ABoxHalStatus ABoxUsbHw_DeviceStall(uint8_t address,uint8_t stall);
ABoxHalStatus ABoxUsbHw_DeviceAddress(uint8_t address);
ABoxHalStatus ABoxUsbHw_DeviceRemoteWakeup(uint8_t asserted);
ABoxHalStatus ABoxUsbHw_HostReset(void);
ABoxHalStatus ABoxUsbHw_HostOpen(unsigned channel,const ABoxUsbHostPipe *pipe);
ABoxHalStatus ABoxUsbHw_HostClose(unsigned channel);
ABoxHalStatus ABoxUsbHw_HostSubmit(unsigned channel,ABoxUsbPid pid,uint8_t *buffer,uint16_t length);
ABoxHalStatus ABoxUsbHw_HostHalt(unsigned channel);
ABoxHalStatus ABoxUsbHw_HostSpeed(ABoxUsbSpeed *speed);
ABoxHalStatus ABoxUsbHw_HostFrame(uint16_t *frame);
void ABoxUsbCore_Event(ABoxUsbEvent event,unsigned number);
void ABoxUsbCore_Setup(const uint8_t setup[8]);
void ABoxUsbCore_Complete(unsigned number,ABoxUsbTransferResult result,uint32_t count);
#endif
