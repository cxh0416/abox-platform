#include <assert.h>
#include <string.h>
#include "abox_usb_backend.h"
#include "abox_usb_otg.h"
#include "abox_usb_otg_backend.h"
#include "abox_usb_otg_port.h"
#include "abox_platform_port.h"

static ABoxUsbOtgRegisters regs;
static unsigned opened,started,paused,adoptions,irqs,cancelled,changed,errors;
static unsigned fail_adopt,last_role,signal;
static uint32_t tick(void *ctx) { (void)ctx;return 1U; }
static ABoxPlatformPort port={.get_tick_ms=tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return &port; }
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t key) { assert(!key); }
ABoxUsbOtgRegisters *ABoxUsbOtgHw_Registers(void) { return &regs; }
static void otg_event(void *c,uint32_t e,const ABoxUsbOtgStatus *s) { (void)c;(void)e;(void)s; }
static void event(void *c,ABoxUsbEvent e,unsigned n)
{ (void)c;if(e==ABOX_USB_ROLE_CHANGED){++changed;last_role=n;}if(e==ABOX_USB_ROLE_ERROR){++errors;assert(n==ABOX_HAL_ERROR);} }
static void complete(void *c,unsigned n,ABoxUsbTransferResult r,uint32_t count)
{ (void)c;(void)n;if(r==ABOX_USB_TRANSFER_CANCELLED){assert(count==0U && paused);++cancelled;}else assert(r==ABOX_USB_TRANSFER_OK && count==64U); }
ABoxHalStatus ABoxUsbHw_Open(const ABoxUsbConfig *c) { assert(c && !opened);opened=1;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_Start(void) { started=1;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_Stop(void) { started=0;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_Close(void) { opened=0;return ABOX_HAL_OK; }
void ABoxUsbHw_Irq(void) { assert(!paused);++irqs; }
void ABoxUsbHw_Poll(void) { assert(!paused); }
void ABoxUsbHw_OtgPause(void) { assert(!paused);paused=1; }
void ABoxUsbHw_OtgSense(void) { }
void ABoxUsbHw_OtgResume(void) { assert(paused);paused=0; }
void ABoxUsbHw_OtgAbort(void) { assert(paused);opened=started=0U; }
ABoxHalStatus ABoxUsbHw_OtgHostSignal(uint8_t suspend,uint8_t resume) { signal=suspend ? 1U : resume ? 2U : 3U;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_OtgAdopt(const ABoxUsbConfig *c)
{ assert(paused && started && c->role==((regs.global_status&1U) ? ABOX_USB_HOST : ABOX_USB_DEVICE));++adoptions;return fail_adopt ? ABOX_HAL_ERROR : ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceOpen(const ABoxUsbDeviceEndpoint *e) { (void)e;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceClose(uint8_t a) { (void)a;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceTransfer(uint8_t a,uint8_t *b,uint16_t n) { (void)a;(void)b;(void)n;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceCancel(uint8_t a) { (void)a;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceStall(uint8_t a,uint8_t s) { (void)a;(void)s;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceAddress(uint8_t a) { (void)a;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceRemoteWakeup(uint8_t a) { (void)a;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostReset(void) { return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostOpen(unsigned n,const ABoxUsbHostPipe *p) { (void)n;(void)p;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostClose(unsigned n) { (void)n;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostSubmit(unsigned n,ABoxUsbPid p,uint8_t *b,uint16_t l) { (void)n;(void)p;(void)b;(void)l;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostHalt(unsigned n) { (void)n;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostSpeed(ABoxUsbSpeed *s) { *s=ABOX_USB_FULL_SPEED;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostFrame(uint16_t *f) { *f=0U;return ABOX_HAL_OK; }

int main(void)
{
    ABoxUsbConfig dev={ABOX_USB_DEVICE,48000000U,128U,{64U,64U,32U,32U},1U,0U};
    ABoxUsbConfig host={ABOX_USB_HOST,48000000U,128U,{64U,64U,0U,0U},1U,0U};
    ABoxUsbEvents e={0,event,0,complete};ABoxUsbOtgEvents oe={0,otg_event};
    ABoxUsbDeviceEndpoint ep={1U,ABOX_USB_BULK,64U};
    ABoxUsbHostPipe pipe={0x81U,1U,ABOX_USB_FULL_SPEED,ABOX_USB_BULK,64U};uint8_t buf[64];
    assert(ABoxUsbOtgPort_Signal(0xffffffffU,1U,0U)==0xffffff91U);
    assert(ABoxUsbOtgPort_Signal(0xffffff91U,0U,1U)==0xffffffd1U);
    assert(ABoxUsbOtgPort_Signal(0xffffffd1U,0U,0U)==0xffffff11U);
    assert(ABoxUsb_Open(&dev,&e)==ABOX_HAL_OK && ABoxUsb_Start()==ABOX_HAL_OK);
    assert(ABoxUsbOtg_EnableManaged(&dev,&oe)==ABOX_HAL_INVALID);
    assert(ABoxUsbOtg_EnableManaged(&host,&oe)==ABOX_HAL_OK);
    assert(ABoxUsbDevice_OpenEndpoint(&ep)==ABOX_HAL_OK);
    assert(ABoxUsbDevice_Transfer(1U,buf,64U,64U)==ABOX_HAL_OK);
    regs.global_status=1U;ABoxUsb_Irq();assert(paused && !irqs);
    assert(ABoxUsb_Stop()==ABOX_HAL_BUSY);
    assert(ABoxUsbDevice_Transfer(1U,buf,64U,64U)==ABOX_HAL_INVALID);
    ABoxUsb_Poll();assert(!paused && adoptions==1U && cancelled==1U && changed==1U && last_role==ABOX_USB_HOST);
    assert(ABoxUsbHost_OpenPipe(1U,&pipe)==ABOX_HAL_OK);
    assert(ABoxUsbHost_Submit(1U,ABOX_USB_DATA0,buf,64U,64U)==ABOX_HAL_OK);
    assert(ABoxUsbOtg_HostSuspend()==ABOX_HAL_BUSY);
    ABoxUsbCore_Complete(1U,ABOX_USB_TRANSFER_OK,64U);
    assert(ABoxUsbOtg_HostSuspend()==ABOX_HAL_OK && signal==1U);
    assert(ABoxUsbHost_Submit(1U,ABOX_USB_DATA0,buf,64U,64U)==ABOX_HAL_BUSY);
    assert(ABoxUsbOtg_HostResume(1U)==ABOX_HAL_OK && signal==2U);
    assert(ABoxUsbOtg_HostResume(0U)==ABOX_HAL_OK && signal==3U);
    assert(ABoxUsbHost_Submit(1U,ABOX_USB_DATA0,buf,64U,64U)==ABOX_HAL_OK);
    regs.global_status=0U;ABoxUsb_Irq();assert(paused);
    ABoxUsb_Poll();assert(!paused && adoptions==2U && cancelled==2U && changed==2U && last_role==ABOX_USB_DEVICE);
    assert(ABoxUsbDevice_OpenEndpoint(&ep)==ABOX_HAL_OK);
    assert(ABoxUsbDevice_Transfer(1U,buf,64U,64U)==ABOX_HAL_OK);
    fail_adopt=1U;regs.global_status=1U;ABoxUsb_Irq();ABoxUsb_Poll();
    assert(paused && cancelled==3U && errors==1U && changed==2U);
    assert(ABoxUsb_Start()==ABOX_HAL_ERROR && ABoxUsb_Close()==ABOX_HAL_OK);
    paused=fail_adopt=0U;memset(&regs,0,sizeof(regs));
    assert(ABoxUsb_Open(&dev,&e)==ABOX_HAL_OK && ABoxUsb_Start()==ABOX_HAL_OK);
    assert(ABoxUsbOtg_EnableManaged(&host,&oe)==ABOX_HAL_OK);
    regs.global_status=1U;ABoxUsb_Poll(); /* Poll also catches a change without an IRQ. */
    assert(!paused && last_role==ABOX_USB_HOST);
    assert(ABoxUsbOtg_HostSuspend()==ABOX_HAL_OK);
    assert(ABoxUsb_Stop()==ABOX_HAL_OK);
    assert((regs.usb_config&(3U<<29))==(1U<<29)); /* force the current, not initial, role */
    assert(ABoxUsb_Start()==ABOX_HAL_OK);
    assert(ABoxUsbOtg_EnableManaged(&dev,&oe)==ABOX_HAL_OK);
    assert(ABoxUsbOtg_HostSuspend()==ABOX_HAL_OK); /* no stale suspend after restart */
    assert(ABoxUsb_Stop()==ABOX_HAL_OK);
    assert(ABoxUsb_Close()==ABOX_HAL_OK);
    return 0;
}
