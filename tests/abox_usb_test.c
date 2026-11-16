#include <assert.h>
#include <stdint.h>
#include "abox_usb_backend.h"
#include "abox_platform_port.h"
static unsigned opened,started,device_opened,host_opened,completion,setup_count,last_number,last_event;
static unsigned cancel_pending,cancel_number,fail_stop,wakeup,suspended;
static ABoxUsbTransferResult last_result;
static uint32_t last_count;
static uint32_t tick(void *c) { (void)c;return 1U; }
static ABoxPlatformPort port={.get_tick_ms=tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return &port; }
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t k) { assert(!k); }
static void event(void *c,ABoxUsbEvent e,unsigned n) { assert(c==(void *)1);last_event=(unsigned)e;last_number=n; }
static void setup(void *c,const uint8_t p[8]) { assert(c==(void *)1 && p[0]==0x80U);++setup_count; }
static void complete(void *c,unsigned n,ABoxUsbTransferResult result,uint32_t count)
{ assert(c==(void *)1);++completion;last_number=n;last_result=result;last_count=count; }
ABoxHalStatus ABoxUsbHw_Open(const ABoxUsbConfig *c) { assert(c && !opened);opened=1;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_Start(void) { assert(opened && !started);started=1;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_Stop(void) { assert(started);started=0;return fail_stop ? ABOX_HAL_ERROR : ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_Close(void) { assert(opened && !started);opened=0;return ABOX_HAL_OK; }
void ABoxUsbHw_Irq(void) { assert(started); }
void ABoxUsbHw_Poll(void)
{ assert(started);if(cancel_pending){cancel_pending=0U;ABoxUsbCore_Complete(cancel_number,ABOX_USB_TRANSFER_CANCELLED,0U);} }
ABoxHalStatus ABoxUsbHw_DeviceOpen(const ABoxUsbDeviceEndpoint *e) { assert(e);++device_opened;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceClose(uint8_t a) { (void)a;assert(device_opened);--device_opened;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceTransfer(uint8_t a,uint8_t *b,uint16_t n) { (void)a;(void)b;(void)n;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceCancel(uint8_t a) { cancel_pending=1U;cancel_number=a;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceStall(uint8_t a,uint8_t v) { (void)a;(void)v;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceAddress(uint8_t a) { (void)a;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_DeviceRemoteWakeup(uint8_t asserted)
{ if(asserted && !suspended)return ABOX_HAL_BUSY;wakeup=asserted;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostReset(void) { return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostOpen(unsigned n,const ABoxUsbHostPipe *p) { assert(n<8 && p);++host_opened;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostClose(unsigned n) { assert(n<8 && host_opened);--host_opened;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostSubmit(unsigned n,ABoxUsbPid p,uint8_t *b,uint16_t l)
{ assert(n<8);(void)p;(void)b;(void)l;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostHalt(unsigned n) { cancel_pending=1U;cancel_number=n;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostSpeed(ABoxUsbSpeed *s) { *s=ABOX_USB_FULL_SPEED;return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostFrame(uint16_t *f) { *f=123U;return ABOX_HAL_OK; }
int main(void)
{
 ABoxUsbEvents e={(void *)1,event,setup,complete};
 ABoxUsbConfig c={ABOX_USB_DEVICE,48000000U,128U,{64U,64U,32U,32U},0U,0U};
 ABoxUsbDeviceEndpoint ep={0x01U,ABOX_USB_BULK,64U};uint8_t b[128]={0x80U};
 c.usb_clock_hz=72000000U;assert(ABoxUsb_Open(&c,&e)==ABOX_HAL_INVALID);c.usb_clock_hz=48000000U;
 c.rx_fifo_words=32U;assert(ABoxUsb_Open(&c,&e)==ABOX_HAL_OK && ABoxUsb_Start()==ABOX_HAL_OK);
 {
  ABoxUsbDeviceEndpoint large={0x01U,ABOX_USB_ISOCHRONOUS,100U};
  assert(ABoxUsbDevice_OpenEndpoint(&large)==ABOX_HAL_UNSUPPORTED);
 }
 assert(ABoxUsb_Stop()==ABOX_HAL_OK && ABoxUsb_Close()==ABOX_HAL_OK);
 c.rx_fifo_words=48U;assert(ABoxUsb_Open(&c,&e)==ABOX_HAL_OK && ABoxUsb_Start()==ABOX_HAL_OK);
 {
  ABoxUsbDeviceEndpoint iso={0x01U,ABOX_USB_ISOCHRONOUS,64U};
  assert(ABoxUsbDevice_OpenEndpoint(&iso)==ABOX_HAL_OK);iso.address=0x02U;
  assert(ABoxUsbDevice_OpenEndpoint(&iso)==ABOX_HAL_UNSUPPORTED);
  assert(ABoxUsbDevice_CloseEndpoint(1U)==ABOX_HAL_OK);
 }
 assert(ABoxUsb_Stop()==ABOX_HAL_OK && ABoxUsb_Close()==ABOX_HAL_OK);c.rx_fifo_words=128U;
 assert(ABoxUsb_Open(&c,&e)==ABOX_HAL_OK && ABoxUsb_Open(&c,&e)==ABOX_HAL_BUSY);
 assert(ABoxUsb_Start()==ABOX_HAL_OK && ABoxUsb_Close()==ABOX_HAL_BUSY);
 assert(ABoxUsbDevice_RemoteWakeup(2U)==ABOX_HAL_INVALID);
 assert(ABoxUsbDevice_RemoteWakeup(1U)==ABOX_HAL_BUSY);suspended=1U;
 assert(ABoxUsbDevice_RemoteWakeup(1U)==ABOX_HAL_OK && wakeup);
 suspended=0U;assert(ABoxUsbDevice_RemoteWakeup(0U)==ABOX_HAL_OK && !wakeup);
 assert(ABoxUsbDevice_OpenEndpoint(&ep)==ABOX_HAL_OK);
 assert(ABoxUsbDevice_Transfer(1U,b,65U,65U)==ABOX_HAL_INVALID);
 assert(ABoxUsbDevice_Transfer(1U,b,65U,128U)==ABOX_HAL_OK);
 assert(ABoxUsbDevice_CloseEndpoint(1U)==ABOX_HAL_BUSY);
 ABoxUsbCore_Complete(1U,ABOX_USB_TRANSFER_OK,64U);
 assert(completion==1U && last_number==1U && last_count==64U && last_result==ABOX_USB_TRANSFER_OK);
 assert(ABoxUsbDevice_Transfer(1U,b,64U,64U)==ABOX_HAL_OK);
 assert(ABoxUsbDevice_Cancel(1U)==ABOX_HAL_OK && completion==1U);
 assert(ABoxUsbDevice_CloseEndpoint(1U)==ABOX_HAL_BUSY);ABoxUsb_Poll();
 assert(completion==2U && last_result==ABOX_USB_TRANSFER_CANCELLED);
 assert(ABoxUsbDevice_SetStall(1U,1U)==ABOX_HAL_OK && ABoxUsbDevice_CloseEndpoint(1U)==ABOX_HAL_OK);
 ABoxUsbCore_Setup(b);assert(setup_count==1U);
 assert(ABoxUsbDevice_OpenEndpoint(&ep)==ABOX_HAL_OK);
 assert(ABoxUsbDevice_Transfer(1U,b,64U,64U)==ABOX_HAL_OK);
 ABoxUsbCore_Event(ABOX_USB_RESET,0U);assert(last_event==ABOX_USB_RESET);
 assert(completion==3U && last_result==ABOX_USB_TRANSFER_CANCELLED);
 assert(ABoxUsbDevice_CloseEndpoint(1U)==ABOX_HAL_INVALID);
 assert(ABoxUsb_Stop()==ABOX_HAL_OK && ABoxUsb_Close()==ABOX_HAL_OK);
 c.role=ABOX_USB_HOST;c.tx_fifo_words[2]=0U;c.tx_fifo_words[3]=0U;
 assert(ABoxUsb_Open(&c,&e)==ABOX_HAL_OK && ABoxUsb_Start()==ABOX_HAL_OK);
 {
  ABoxUsbHostPipe p={0x81U,5U,ABOX_USB_FULL_SPEED,ABOX_USB_BULK,64U};ABoxUsbSpeed speed;uint16_t frame;
  assert(ABoxUsbHost_OpenPipe(8U,&p)==ABOX_HAL_INVALID);
  assert(ABoxUsbHost_OpenPipe(1U,&p)==ABOX_HAL_OK);
  assert(ABoxUsbHost_Submit(1U,ABOX_USB_SETUP,b,8U,128U)==ABOX_HAL_INVALID);
  assert(ABoxUsbHost_Submit(1U,ABOX_USB_DATA0,b,65U,128U)==ABOX_HAL_UNSUPPORTED);
  assert(ABoxUsbHost_Submit(1U,ABOX_USB_DATA0,b,32U,32U)==ABOX_HAL_INVALID);
  assert(ABoxUsbHost_Submit(1U,ABOX_USB_DATA0,b,32U,64U)==ABOX_HAL_OK);
  assert(ABoxUsbHost_ClosePipe(1U)==ABOX_HAL_BUSY);
  ABoxUsbCore_Complete(1U,ABOX_USB_TRANSFER_NAK,0U);
  assert(completion==4U && last_result==ABOX_USB_TRANSFER_NAK);
  assert(ABoxUsbHost_Submit(1U,ABOX_USB_DATA1,b,64U,64U)==ABOX_HAL_OK);
  assert(ABoxUsbHost_Halt(1U)==ABOX_HAL_OK && ABoxUsbHost_ClosePipe(1U)==ABOX_HAL_BUSY);
  ABoxUsb_Poll();assert(completion==5U && last_result==ABOX_USB_TRANSFER_CANCELLED);
  assert(ABoxUsbHost_ClosePipe(1U)==ABOX_HAL_OK);
  p.speed=ABOX_USB_LOW_SPEED;assert(ABoxUsbHost_OpenPipe(1U,&p)==ABOX_HAL_INVALID);
  p.type=ABOX_USB_CONTROL;p.address=0U;p.max_packet=4U;assert(ABoxUsbHost_OpenPipe(1U,&p)==ABOX_HAL_INVALID);
  p.max_packet=8U;assert(ABoxUsbHost_OpenPipe(1U,&p)==ABOX_HAL_OK);assert(ABoxUsbHost_ClosePipe(1U)==ABOX_HAL_OK);
  assert(ABoxUsbHost_PortSpeed(&speed)==ABOX_HAL_OK && speed==ABOX_USB_FULL_SPEED);
  assert(ABoxUsbHost_Frame(&frame)==ABOX_HAL_OK && frame==123U);
 }
 assert(ABoxUsb_Stop()==ABOX_HAL_OK && ABoxUsb_Close()==ABOX_HAL_OK);
 c.tx_fifo_words[1]=16U;assert(ABoxUsb_Open(&c,&e)==ABOX_HAL_OK && ABoxUsb_Start()==ABOX_HAL_OK);
 {
  ABoxUsbHostPipe iso={0x01U,5U,ABOX_USB_FULL_SPEED,ABOX_USB_ISOCHRONOUS,64U};
  assert(ABoxUsbHost_OpenPipe(1U,&iso)==ABOX_HAL_UNSUPPORTED);
 }
 assert(ABoxUsb_Stop()==ABOX_HAL_OK);
 assert(ABoxUsb_Start()==ABOX_HAL_OK);
 fail_stop=1U;assert(ABoxUsb_Stop()==ABOX_HAL_ERROR);
 assert(ABoxUsb_Start()==ABOX_HAL_ERROR && ABoxUsb_Close()==ABOX_HAL_OK);fail_stop=0U;
 return 0;
}
