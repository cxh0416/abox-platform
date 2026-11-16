#include "abox_usb_backend.h"
#include "abox_platform_port.h"
#include "drv_usb_core.h"
#include "drv_usb_dev.h"
#include "drv_usb_host.h"
#include "drv_usbd_int.h"
#include "drv_usbh_int.h"
#include "abox_usb_gd_hooks.h"
#include <string.h>
#ifdef ABOX_USB_OTG_MANAGED
#include "abox_usb_otg_port.h"
#endif

/* The vendor core is a controller driver. Class requests and VBUS switching
 * remain with the application and board, respectively. */
static usb_core_driver controller;
static ABoxUsbRole role;
static uint8_t sof_enabled, pending_reset, pending_setup, pending_in, pending_out;
static uint8_t pending_errors, host_active, cancelling, halting, host_errors;
static uint8_t device_cancel[2];
static uint16_t device_in_length[4], host_length[8];
static uint8_t host_direction[8];
ABoxHalStatus ABoxUsbHw_DeviceRemoteWakeup(uint8_t asserted)
{
    if(asserted && !(controller.regs.dr->DSTAT & DSTAT_SPST))return ABOX_HAL_BUSY;
    if(asserted)controller.regs.dr->DCTL |= DCTL_RWKUP;else controller.regs.dr->DCTL &= ~DCTL_RWKUP;
    return ABOX_HAL_OK;
}

void usb_udelay(const uint32_t usec)
{
    uint32_t count=(rcu_clock_freq_get(CK_AHB)/1000000U)*usec;
    while (count--) __NOP();
}
void usb_mdelay(const uint32_t msec)
{
    uint32_t start=ABox_PortGetTickMs();
    while ((uint32_t)(ABox_PortGetTickMs()-start)<msec) { }
}
void usb_vbus_drive(uint8_t state) { (void)state; }
void system_clk_config_stop(void) { }

void ABoxUsbGd_Discard(usb_core_driver *udev,uint32_t count)
{
    while (count) {
        volatile uint32_t word=*udev->regs.DFIFO[0];
        (void)word;
        count=count>4U ? count-4U : 0U;
    }
}
void ABoxUsbGd_ReceiveError(unsigned host,unsigned number)
{
    if (host && number<8U) host_errors|=(uint8_t)(1U<<number);
    else if (!host && number<4U) pending_errors|=(uint8_t)(1U<<number);
}
void ABoxUsbGd_DeviceSession(unsigned connected)
{ ABoxUsbCore_Event(connected ? ABOX_USB_CONNECTED : ABOX_USB_DISCONNECTED,0U); }

static uint8_t class_sof(usb_dev *device)
{ (void)device;if(sof_enabled)ABoxUsbCore_Event(ABOX_USB_SOF,0U);return 0U; }
static uint8_t class_iso_in(usb_dev *device)
{ (void)device;ABoxUsbCore_Event(ABOX_USB_ISO_INCOMPLETE,ABOX_USB_ANY_IN_ENDPOINT);return 0U; }
static uint8_t class_iso_out(usb_dev *device)
{ (void)device;ABoxUsbCore_Event(ABOX_USB_ISO_INCOMPLETE,ABOX_USB_ANY_OUT_ENDPOINT);return 0U; }
static usb_class_core class_core={.SOF=class_sof,.incomplete_isoc_in=class_iso_in,.incomplete_isoc_out=class_iso_out};
uint8_t usbd_setup_transc(usb_core_driver *device) { (void)device;pending_setup=1U;return 0U; }
uint8_t usbd_out_transc(usb_core_driver *device,uint8_t n)
{ (void)device;if(n<4U)pending_out|=(uint8_t)(1U<<n);return 0U; }
uint8_t usbd_in_transc(usb_core_driver *device,uint8_t n)
{ (void)device;if(n<4U)pending_in|=(uint8_t)(1U<<n);return 0U; }
static uint8_t host_connect(usbh_host *host) { (void)host;ABoxUsbCore_Event(ABOX_USB_CONNECTED,0U);return 0U; }
static uint8_t host_disconnect(usbh_host *host) { (void)host;ABoxUsbCore_Event(ABOX_USB_DISCONNECTED,0U);return 0U; }
static uint8_t host_sof(usbh_host *host)
{ (void)host;if(sof_enabled)ABoxUsbCore_Event(ABOX_USB_SOF,0U);return 0U; }
static usbh_ev_cb host_callbacks={.connect=host_connect,.disconnect=host_disconnect,.SOF=host_sof};
usbh_ev_cb *usbh_int_fop=&host_callbacks;

static void deliver(void)
{
    if (controller.regs.gr->GINTF&GINTF_RXFNEIF) return;
    if (role==ABOX_USB_DEVICE) {
        if (pending_reset) { pending_reset=0U;ABoxUsbCore_Event(ABOX_USB_RESET,0U); }
        if (pending_setup) { pending_setup=0U;ABoxUsbCore_Setup((const uint8_t *)&controller.dev.control.req); }
        for (unsigned n=0U;n<4U;++n) {
            uint8_t mask=(uint8_t)(1U<<n);
            for(unsigned dir=0U;dir<2U;++dir) if(device_cancel[dir]&mask) {
                volatile uint32_t *ctl=dir ? &controller.regs.er_in[n]->DIEPCTL : &controller.regs.er_out[n]->DOEPCTL;
                ABoxUsbTransferResult outcome=ABOX_USB_TRANSFER_CANCELLED;
                if(*ctl&DEPCTL_EPEN) { *ctl|=DEPCTL_SNAK|DEPCTL_EPD;continue; }
                if(dir && usb_txfifo_flush(&controller.regs,(uint8_t)n)!=USB_OK)outcome=ABOX_USB_TRANSFER_ERROR;
                if(!dir && n==0U)usb_ctlep_startout(&controller);
                device_cancel[dir]&=(uint8_t)~mask;
                if(dir)pending_in&=(uint8_t)~mask;else { pending_out&=(uint8_t)~mask;pending_errors&=(uint8_t)~mask;controller.dev.transc_out[n].xfer_buf=NULL;controller.dev.transc_out[n].remain_len=0U; }
                ABoxUsbCore_Complete(n|(dir ? 0x80U : 0U),outcome,0U);
            }
            if ((pending_errors&mask) && !(device_cancel[0]&mask)) {
                if (controller.regs.er_out[n]->DOEPCTL&DEPCTL_EPEN) {
                    controller.regs.er_out[n]->DOEPCTL|=DEPCTL_SNAK|DEPCTL_EPD;
                    continue;
                }
                pending_errors&=(uint8_t)~mask;pending_out&=(uint8_t)~mask;
                controller.dev.transc_out[n].xfer_buf=NULL;controller.dev.transc_out[n].remain_len=0U;
                ABoxUsbCore_Complete(n,ABOX_USB_TRANSFER_ERROR,controller.dev.transc_out[n].xfer_count);
            }
            if ((pending_out&mask) && !(device_cancel[0]&mask)) {
                pending_out&=(uint8_t)~mask;
                controller.dev.transc_out[n].xfer_buf=NULL;controller.dev.transc_out[n].remain_len=0U;
                ABoxUsbCore_Complete(n,ABOX_USB_TRANSFER_OK,controller.dev.transc_out[n].xfer_count);
            }
            if ((pending_in&mask) && !(device_cancel[1]&mask)) {
                pending_in&=(uint8_t)~mask;
                ABoxUsbCore_Complete(n|0x80U,ABOX_USB_TRANSFER_OK,device_in_length[n]);
            }
        }
    } else {
        for (unsigned n=0U;n<8U;++n) {
            uint8_t mask=(uint8_t)(1U<<n);usb_pipe *p=&controller.host.pipe[n];
            ABoxUsbTransferResult result;
            if (!(host_active&mask)) continue;
            if ((cancelling|host_errors)&mask) {
                if (controller.regs.pr[n]->HCHCTL&HCHCTL_CEN) {
                    if (!(halting&mask)) { halting|=mask;(void)usb_pipe_halt(&controller,(uint8_t)n); }
                    continue;
                }
                result=(cancelling&mask) ? ABOX_USB_TRANSFER_CANCELLED : ABOX_USB_TRANSFER_ERROR;
            } else if (p->urb_state==URB_DONE) result=ABOX_USB_TRANSFER_OK;
            else if (p->urb_state==URB_STALL) result=ABOX_USB_TRANSFER_STALL;
            else if (p->urb_state==URB_ERROR) result=ABOX_USB_TRANSFER_ERROR;
            else if (p->urb_state==URB_NOTREADY) result=ABOX_USB_TRANSFER_NAK;
            else if (!(controller.regs.pr[n]->HCHCTL&HCHCTL_CEN) && p->pp_status!=PIPE_IDLE) {
                result=p->pp_status==PIPE_NAK || p->pp_status==PIPE_NYET ? ABOX_USB_TRANSFER_NAK :
                       p->pp_status==PIPE_STALL ? ABOX_USB_TRANSFER_STALL :
                       p->pp_status==PIPE_XF ? ABOX_USB_TRANSFER_OK : ABOX_USB_TRANSFER_ERROR;
            } else continue;
            if (controller.regs.pr[n]->HCHCTL&HCHCTL_CEN) continue;
            controller.regs.pr[n]->HCHINTF=0xffffffffU;
            host_active&=(uint8_t)~mask;cancelling&=(uint8_t)~mask;halting&=(uint8_t)~mask;host_errors&=(uint8_t)~mask;p->xfer_buf=NULL;
            ABoxUsbCore_Complete(n,result,host_direction[n] ? p->xfer_count : result==ABOX_USB_TRANSFER_OK ? host_length[n] : 0U);
        }
    }
}

ABoxHalStatus ABoxUsbHw_Open(const ABoxUsbConfig *c)
{
    uint32_t start;
    if (rcu_clock_freq_get(CK_AHB)<14200000U) return ABOX_HAL_UNSUPPORTED;
    if (c->role==ABOX_USB_DEVICE && !c->vbus_sensing) return ABOX_HAL_UNSUPPORTED;
    memset(&controller,0,sizeof(controller));role=c->role;sof_enabled=c->sof_events;
    pending_reset=pending_setup=pending_in=pending_out=pending_errors=host_active=cancelling=halting=host_errors=0U;
    device_cancel[0]=device_cancel[1]=0U;
    rcu_periph_clock_enable(RCU_USBFS);
    (void)usb_basic_init(&controller.bp,&controller.regs);
    controller.dev.class_core=&class_core;
    usb_globalint_disable(&controller.regs);
    if (usb_core_init(controller.bp,&controller.regs)!=USB_OK) goto fail;
    controller.regs.gr->GCCFG&=~(GCCFG_VBUSACEN|GCCFG_VBUSBCEN);
    if (c->vbus_sensing) controller.regs.gr->GCCFG|=role==ABOX_USB_DEVICE ? GCCFG_VBUSBCEN : GCCFG_VBUSACEN;
    usb_curmode_set(&controller.regs,role==ABOX_USB_DEVICE ? DEVICE_MODE : HOST_MODE);
    usb_mdelay(25U);start=ABox_PortGetTickMs();
    while (((controller.regs.gr->GINTF&GINTF_COPM)!=0U)!=(role==ABOX_USB_HOST))
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=100U) goto fail;
    if (role==ABOX_USB_DEVICE) {
        usb_dev_disconnect(&controller);
        if (usb_devcore_init(&controller)!=USB_OK) goto fail;
        usb_set_rxfifo(&controller.regs,c->rx_fifo_words);
        for(unsigned i=0U;i<4U;++i)usb_set_txfifo(&controller.regs,(uint8_t)i,c->tx_fifo_words[i]);
    } else {
        if (usb_host_init(&controller)!=USB_OK) goto fail;
        controller.regs.gr->GRFLEN=c->rx_fifo_words;
        controller.regs.gr->DIEP0TFLEN_HNPTFLEN=((uint32_t)c->tx_fifo_words[0]<<16U)|c->rx_fifo_words;
        controller.regs.gr->HPTFLEN=((uint32_t)c->tx_fifo_words[1]<<16U)|(c->rx_fifo_words+c->tx_fifo_words[0]);
        controller.regs.gr->GINTEN=(controller.regs.gr->GINTEN|GINTEN_DISCIE)&~GINTEN_SESIE;
    }
    return ABOX_HAL_OK;
fail:
    usb_globalint_disable(&controller.regs);rcu_periph_clock_disable(RCU_USBFS);return ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxUsbHw_Start(void)
{
    if(role==ABOX_USB_DEVICE)usb_dev_connect(&controller);else usb_portvbus_switch(&controller,1U);
    usb_globalint_enable(&controller.regs);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_Stop(void)
{
    usb_status status;
    usb_globalint_disable(&controller.regs);
    if(role==ABOX_USB_DEVICE) { usb_dev_disconnect(&controller);status=usb_dev_stop(&controller); }
    else { status=usb_host_stop(&controller);usb_portvbus_switch(&controller,0U); }
    return status==USB_OK ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxUsbHw_Close(void)
{
    usb_globalint_disable(&controller.regs);rcu_periph_reset_enable(RCU_USBFSRST);
    rcu_periph_reset_disable(RCU_USBFSRST);rcu_periph_clock_disable(RCU_USBFS);return ABOX_HAL_OK;
}
void ABoxUsbHw_Irq(void)
{
    uint32_t flags=controller.regs.gr->GINTF&controller.regs.gr->GINTEN;
    uint32_t enabled=controller.host.port_enabled;
    if(role==ABOX_USB_DEVICE) {
        usbd_isr(&controller);
        if(flags&GINTF_RST) {
            pending_setup=pending_in=pending_out=pending_errors=0U;device_cancel[0]=device_cancel[1]=0U;
            for(unsigned n=0U;n<4U;++n) { controller.dev.transc_out[n].xfer_buf=NULL;controller.dev.transc_out[n].remain_len=0U; }
            pending_reset=1U;
        }
        if(flags&GINTF_SP)ABoxUsbCore_Event(ABOX_USB_SUSPEND,0U);
        if(flags&GINTF_WKUPIF)ABoxUsbCore_Event(ABOX_USB_RESUME,0U);
    } else {
        (void)usbh_isr(&controller);
        if(enabled!=controller.host.port_enabled)ABoxUsbCore_Event(controller.host.port_enabled ? ABOX_USB_PORT_ENABLED : ABOX_USB_PORT_DISABLED,0U);
    }
    deliver();
}
void ABoxUsbHw_Poll(void) { deliver(); }
#ifdef ABOX_USB_OTG_MANAGED
void ABoxUsbHw_OtgPause(void) { usb_globalint_disable(&controller.regs); }
void ABoxUsbHw_OtgSense(void) { controller.regs.gr->GCCFG |= GCCFG_VBUSACEN|GCCFG_VBUSBCEN; }
void ABoxUsbHw_OtgResume(void) { usb_globalint_enable(&controller.regs); }
void ABoxUsbHw_OtgAbort(void)
{ usb_globalint_disable(&controller.regs);rcu_periph_reset_enable(RCU_USBFSRST);rcu_periph_reset_disable(RCU_USBFSRST);rcu_periph_clock_disable(RCU_USBFS); }
ABoxHalStatus ABoxUsbHw_OtgHostSignal(uint8_t suspend,uint8_t resume)
{
    *controller.regs.HPCS=ABoxUsbOtgPort_Signal(*controller.regs.HPCS,suspend,resume);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_OtgAdopt(const ABoxUsbConfig *c)
{
    usb_status status;uint32_t otg_mask=controller.regs.gr->GINTEN & ((1UL<<2)|(1UL<<28)|(1UL<<30));
    if (!!(controller.regs.gr->GINTF&GINTF_COPM)!=(c->role==ABOX_USB_HOST)) return ABOX_HAL_BUSY;
    role=c->role;sof_enabled=c->sof_events;
    pending_reset=pending_setup=pending_in=pending_out=pending_errors=host_active=cancelling=halting=host_errors=0U;
    device_cancel[0]=device_cancel[1]=0U;
    memset(&controller.dev,0,sizeof(controller.dev));memset(&controller.host,0,sizeof(controller.host));
    controller.dev.class_core=&class_core;
    ABoxUsbHw_OtgSense();
    if (role==ABOX_USB_DEVICE) {
        status=usb_devcore_init(&controller);
        if(status==USB_OK) {
            usb_set_rxfifo(&controller.regs,c->rx_fifo_words);
            for(unsigned i=0U;i<4U;++i)usb_set_txfifo(&controller.regs,(uint8_t)i,c->tx_fifo_words[i]);
            usb_dev_connect(&controller);
        }
    } else {
        status=usb_host_init(&controller);
        if(status==USB_OK) {
            controller.regs.gr->GRFLEN=c->rx_fifo_words;
            controller.regs.gr->DIEP0TFLEN_HNPTFLEN=((uint32_t)c->tx_fifo_words[0]<<16U)|c->rx_fifo_words;
            controller.regs.gr->HPTFLEN=((uint32_t)c->tx_fifo_words[1]<<16U)|(c->rx_fifo_words+c->tx_fifo_words[0]);
            controller.regs.gr->GINTEN=(controller.regs.gr->GINTEN|GINTEN_DISCIE)&~GINTEN_SESIE;
            if(c->sof_events)controller.regs.gr->GINTEN|=GINTEN_SOFIE;
            *controller.regs.HPCS=usb_port_read(&controller)|HPCS_PP; /* internal port power */
        }
    }
    controller.regs.gr->GINTEN|=otg_mask;
    if (!!(controller.regs.gr->GINTF&GINTF_COPM)!=(c->role==ABOX_USB_HOST)) return ABOX_HAL_BUSY;
    return status==USB_OK ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
#endif
ABoxHalStatus ABoxUsbHw_DeviceOpen(const ABoxUsbDeviceEndpoint *e)
{
    unsigned n=e->address&15U;usb_transc *t=(e->address&0x80U) ? &controller.dev.transc_in[n] : &controller.dev.transc_out[n];
    memset(t,0,sizeof(*t));t->ep_addr.num=n;t->ep_addr.dir=(e->address>>7U);t->ep_type=(uint8_t)e->type;t->max_len=e->max_packet;
    if (!n) {
        volatile uint32_t *ctl=t->ep_addr.dir ? &controller.regs.er_in[0]->DIEPCTL : &controller.regs.er_out[0]->DOEPCTL;
        *ctl=(*ctl&~3U)|(e->max_packet==64U ? 0U : e->max_packet==32U ? 1U : e->max_packet==16U ? 2U : 3U);
    }
    return usb_transc_active(&controller,t)==USB_OK ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxUsbHw_DeviceClose(uint8_t address)
{
    unsigned n=address&15U;usb_transc *t=(address&0x80U) ? &controller.dev.transc_in[n] : &controller.dev.transc_out[n];
    return usb_transc_deactivate(&controller,t)==USB_OK ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxUsbHw_DeviceTransfer(uint8_t address,uint8_t *buffer,uint16_t length)
{
    unsigned n=address&15U;usb_transc *t=(address&0x80U) ? &controller.dev.transc_in[n] : &controller.dev.transc_out[n];
    t->xfer_buf=buffer;t->xfer_len=length;t->xfer_count=0U;
    t->remain_len=length ? (((uint32_t)length+t->max_len-1U)/t->max_len)*t->max_len : t->max_len;
    if (address&0x80U) device_in_length[n]=length;
    return ((address&0x80U) ? usb_transc_inxfer(&controller,t) : usb_transc_outxfer(&controller,t))==USB_OK ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxUsbHw_DeviceCancel(uint8_t address)
{
    unsigned n=address&15U,dir=address>>7U;uint8_t mask=(uint8_t)(1U<<n);
    volatile uint32_t *ctl=dir ? &controller.regs.er_in[n]->DIEPCTL : &controller.regs.er_out[n]->DOEPCTL;
    if(controller.regs.gr->GINTF&GINTF_RXFNEIF)return ABOX_HAL_BUSY;
    device_cancel[dir]|=mask;
    if(dir)controller.regs.dr->DIEPFEINTEN&=~mask;
    if(*ctl&DEPCTL_EPEN)*ctl|=DEPCTL_SNAK|DEPCTL_EPD;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_DeviceStall(uint8_t address,uint8_t stall)
{
    unsigned n=address&15U;usb_transc *t=(address&0x80U) ? &controller.dev.transc_in[n] : &controller.dev.transc_out[n];
    return (stall ? usb_transc_stall(&controller,t) : usb_transc_clrstall(&controller,t))==USB_OK ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxUsbHw_DeviceAddress(uint8_t address) { usb_devaddr_set(&controller,address);return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostReset(void) { (void)usb_port_reset(&controller);return ABOX_HAL_OK; }
ABoxHalStatus ABoxUsbHw_HostOpen(unsigned n,const ABoxUsbHostPipe *pipe)
{
    usb_pipe *p=&controller.host.pipe[n];memset(p,0,sizeof(*p));
    p->dev_addr=pipe->device_address;p->dev_speed=pipe->speed==ABOX_USB_LOW_SPEED ? PORT_SPEED_LOW : PORT_SPEED_FULL;
    p->ep.num=pipe->address&15U;p->ep.dir=(pipe->address>>7U);p->ep.type=(uint8_t)pipe->type;p->ep.mps=pipe->max_packet;
    host_direction[n]=p->ep.dir;return usb_pipe_init(&controller,(uint8_t)n)==USB_OK ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxUsbHw_HostClose(unsigned n)
{
    controller.regs.pr[n]->HCHINTEN=0U;controller.regs.pr[n]->HCHINTF=0xffffffffU;
    controller.regs.hr->HACHINTEN&=~(1U<<n);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_HostSubmit(unsigned n,ABoxUsbPid pid,uint8_t *buffer,uint16_t length)
{
    usb_pipe *p=&controller.host.pipe[n];uint32_t fifo=(p->ep.type==USB_EPTYPE_INTR || p->ep.type==USB_EPTYPE_ISOC) ?
        controller.regs.hr->HPTFQSTAT : controller.regs.gr->HNPTFQSTAT;
    if (!(fifo&0x00ff0000U) || (!p->ep.dir && (fifo&0xffffU)<(length+3U)/4U)) return ABOX_HAL_BUSY;
    p->DPID=pid==ABOX_USB_SETUP ? PIPE_DPID_SETUP : pid==ABOX_USB_DATA1 ? PIPE_DPID_DATA1 : PIPE_DPID_DATA0;
    p->xfer_buf=buffer;p->xfer_len=length;p->xfer_count=0U;p->pp_status=PIPE_IDLE;p->urb_state=URB_IDLE;
    host_length[n]=length;host_active|=(uint8_t)(1U<<n);
    if (usb_pipe_xfer(&controller,(uint8_t)n)!=USB_OK) { host_active&=(uint8_t)~(1U<<n);return ABOX_HAL_ERROR; }
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_HostHalt(unsigned n)
{
    if(controller.regs.gr->GINTF&GINTF_RXFNEIF)return ABOX_HAL_BUSY;
    cancelling|=(uint8_t)(1U<<n);
    if(controller.regs.pr[n]->HCHCTL&HCHCTL_CEN) { halting|=(uint8_t)(1U<<n);(void)usb_pipe_halt(&controller,(uint8_t)n); }
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_HostSpeed(ABoxUsbSpeed *speed)
{
    uint32_t s=usb_curspeed_get(&controller);
    if(s==PORT_SPEED_FULL)*speed=ABOX_USB_FULL_SPEED;else if(s==PORT_SPEED_LOW)*speed=ABOX_USB_LOW_SPEED;else return ABOX_HAL_UNSUPPORTED;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_HostFrame(uint16_t *frame) { *frame=(uint16_t)usb_curframe_get(&controller);return ABOX_HAL_OK; }
