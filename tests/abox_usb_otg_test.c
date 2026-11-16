#include <assert.h>
#include "abox_usb_otg.h"
#include "abox_usb_otg_backend.h"
static ABoxUsbOtgRegisters regs;static unsigned calls;static uint32_t last_events;
static ABoxUsbOtgStatus last_status;
ABoxUsbOtgRegisters *ABoxUsbOtgHw_Registers(void) { return &regs; }
static void event(void *ctx,uint32_t events,const ABoxUsbOtgStatus *status)
{ assert(ctx==(void *)0x1234);++calls;last_events=events;last_status=*status; }
int main(void)
{
    ABoxUsbOtgEvents e={(void *)0x1234,event};ABoxUsbOtgStatus s;
    regs.usb_config=(1U<<29)|0x40U;regs.global_mask=0x80U;
    assert(ABoxUsbOtg_RequestSrp()==ABOX_HAL_INVALID);
    assert(ABoxUsbOtg_Enable(&e)==ABOX_HAL_OK);
    assert(!(regs.usb_config&(3U<<29)) && (regs.usb_config&0x300U)==0x300U);
    assert(ABoxUsbOtg_Enable(&e)==ABOX_HAL_BUSY);
    regs.control=1U<<16;regs.global_status=0U;
    assert(ABoxUsbOtg_RequestHnp()==ABOX_HAL_BUSY);
    assert(ABoxUsbOtg_SetHnpPermission(1U)==ABOX_HAL_OK && (regs.control&(1U<<11)));
    assert(ABoxUsbOtg_RequestHnp()==ABOX_HAL_OK && (regs.control&(1U<<9)));
    assert(ABoxUsbOtg_RequestSrp()==ABOX_HAL_OK && (regs.control&(1U<<1)));
    regs.control|=1U<<19;assert(ABoxUsbOtg_RequestSrp()==ABOX_HAL_BUSY);
    regs.control|=(1U<<18)|(1U<<8)|1U;
    assert(ABoxUsbOtg_GetStatus(&s)==ABOX_HAL_OK && !s.host && s.b_device && s.b_session_valid && s.hnp_success && s.srp_success);
    regs.global_status=(1U<<2)|(1U<<28)|1U;regs.otg_interrupt=(1U<<9)|(1U<<19);
    ABoxUsbOtg_Irq();assert(calls==1U && last_events==((1U<<28)|(1U<<9)|(1U<<19)) && last_status.host);
    assert(regs.otg_interrupt==((1U<<9)|(1U<<19)) && regs.global_status==(1U<<28));
    regs.global_status=1U;assert(ABoxUsbOtg_SetHnpPermission(1U)==ABOX_HAL_OK && (regs.control&(1U<<10)));
    assert(ABoxUsbOtg_RequestHnp()==ABOX_HAL_BUSY);
    assert(ABoxUsbOtg_Disable()==ABOX_HAL_OK && regs.usb_config==((1U<<29)|0x40U) && regs.global_mask==0x80U);
    return 0;
}
