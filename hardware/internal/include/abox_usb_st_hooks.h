#ifndef ABOX_USB_ST_HOOKS_H
#define ABOX_USB_ST_HOOKS_H
#include <stdint.h>
void ABoxUsbSt_Discard(uint32_t count);
int ABoxUsbSt_DeviceReceiveValid(unsigned endpoint,uint32_t used,uint32_t count);
int ABoxUsbSt_HostReceiveValid(unsigned channel);
void ABoxUsbSt_ReceiveError(unsigned host,unsigned number);
#endif
