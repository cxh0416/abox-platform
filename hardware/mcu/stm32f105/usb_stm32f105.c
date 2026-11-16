#include "abox_usb_backend.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_pcd.h"
#include "stm32f1xx_hal_hcd.h"
#include "abox_usb_st_hooks.h"
#include <string.h>
#ifdef ABOX_USB_OTG_MANAGED
#include "abox_usb_otg_port.h"
#endif

static union { PCD_HandleTypeDef device; HCD_HandleTypeDef host; } peripheral;
static ABoxUsbRole role;
static uint8_t sof_enabled,pending_reset,pending_setup,pending_in,pending_out,pending_errors,pending_host,host_errors,cancelling,halting;
static uint8_t device_cancel[2];
static uint8_t device_rx_active,host_active;
static uint8_t host_result[8],host_direction[8];
static uint16_t host_length[8],device_in_length[4];
static uint32_t device_capacity[4];
static ABoxHalStatus result(HAL_StatusTypeDef s)
{ return s==HAL_OK ? ABOX_HAL_OK : s==HAL_BUSY ? ABOX_HAL_BUSY : s==HAL_TIMEOUT ? ABOX_HAL_TIMEOUT : ABOX_HAL_ERROR; }
static USB_OTG_HostChannelTypeDef *channel(unsigned n)
{ return (USB_OTG_HostChannelTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_HOST_CHANNEL_BASE+n*USB_OTG_HOST_CHANNEL_SIZE); }
static USB_OTG_HostTypeDef *host_registers(void)
{ return (USB_OTG_HostTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_HOST_BASE); }
ABoxHalStatus ABoxUsbHw_DeviceRemoteWakeup(uint8_t asserted)
{
    USB_OTG_DeviceTypeDef *d=(USB_OTG_DeviceTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_DEVICE_BASE);
    if(asserted && !(d->DSTS & USB_OTG_DSTS_SUSPSTS))return ABOX_HAL_BUSY;
    if(asserted)d->DCTL |= USB_OTG_DCTL_RWUSIG;else d->DCTL &= ~USB_OTG_DCTL_RWUSIG;
    return ABOX_HAL_OK;
}
void ABoxUsbSt_Discard(uint32_t count)
{
    volatile uint32_t *fifo=(volatile uint32_t *)((uintptr_t)USB_OTG_FS+USB_OTG_FIFO_BASE);
    while(count) { volatile uint32_t word=*fifo;(void)word;count=count>4U ? count-4U : 0U; }
}
int ABoxUsbSt_DeviceReceiveValid(unsigned n,uint32_t used,uint32_t count)
{ return n<4U && (device_rx_active&(1U<<n)) && used<=device_capacity[n] && count<=device_capacity[n]-used; }
int ABoxUsbSt_HostReceiveValid(unsigned n) { return n<8U && (host_active&(1U<<n)); }
void ABoxUsbSt_ReceiveError(unsigned host,unsigned number)
{
    if(host && number<8U) { host_errors|=(uint8_t)(1U<<number);pending_host|=(uint8_t)(1U<<number); }
    else if(!host && number<4U) pending_errors|=(uint8_t)(1U<<number);
}
static void deliver(void)
{
    if (USB_OTG_FS->GINTSTS & USB_OTG_GINTSTS_RXFLVL) return;
    if (role==ABOX_USB_DEVICE) {
        if (pending_reset) { pending_reset=0U;ABoxUsbCore_Event(ABOX_USB_RESET,0U); }
        if (pending_setup) { pending_setup=0U;ABoxUsbCore_Setup((const uint8_t *)peripheral.device.Setup); }
        for (unsigned n=0U;n<4U;++n) {
            uint8_t mask=(uint8_t)(1U<<n);
            for (unsigned dir=0U;dir<2U;++dir) if (device_cancel[dir]&mask) {
                volatile uint32_t *ctl=dir ? &((USB_OTG_INEndpointTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_IN_ENDPOINT_BASE+n*USB_OTG_EP_REG_SIZE))->DIEPCTL :
                                              &((USB_OTG_OUTEndpointTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_OUT_ENDPOINT_BASE+n*USB_OTG_EP_REG_SIZE))->DOEPCTL;
                ABoxUsbTransferResult outcome=ABOX_USB_TRANSFER_CANCELLED;
                if (*ctl&(1UL<<31U)) { *ctl|=(1UL<<27U)|(1UL<<30U);continue; }
                if (dir && USB_FlushTxFifo(USB_OTG_FS,n)!=HAL_OK) outcome=ABOX_USB_TRANSFER_ERROR;
                if (!dir && n==0U) (void)USB_EP0_OutStart(USB_OTG_FS,(uint8_t *)peripheral.device.Setup);
                device_cancel[dir]&=(uint8_t)~mask;
                if(dir)pending_in&=(uint8_t)~mask;else {
                    pending_out&=(uint8_t)~mask;pending_errors&=(uint8_t)~mask;
                    device_rx_active&=(uint8_t)~mask;peripheral.device.OUT_ep[n].xfer_buff=NULL;
                }
                ABoxUsbCore_Complete(n|(dir ? 0x80U : 0U),outcome,0U);
            }
            if ((pending_errors&mask) && !(device_cancel[0]&mask)) {
                USB_OTG_OUTEndpointTypeDef *ep=(USB_OTG_OUTEndpointTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_OUT_ENDPOINT_BASE+n*USB_OTG_EP_REG_SIZE);
                if (ep->DOEPCTL&USB_OTG_DOEPCTL_EPENA) { ep->DOEPCTL|=USB_OTG_DOEPCTL_SNAK|USB_OTG_DOEPCTL_EPDIS;continue; }
                pending_errors&=(uint8_t)~mask;pending_out&=(uint8_t)~mask;
                device_rx_active&=(uint8_t)~mask;peripheral.device.OUT_ep[n].xfer_buff=NULL;
                ABoxUsbCore_Complete(n,ABOX_USB_TRANSFER_ERROR,peripheral.device.OUT_ep[n].xfer_count);
            }
            if ((pending_out&mask) && !(device_cancel[0]&mask)) {
                pending_out&=(uint8_t)~mask;
                device_rx_active&=(uint8_t)~mask;peripheral.device.OUT_ep[n].xfer_buff=NULL;
                ABoxUsbCore_Complete(n,ABOX_USB_TRANSFER_OK,peripheral.device.OUT_ep[n].xfer_count);
            }
            if ((pending_in&mask) && !(device_cancel[1]&mask)) {
                pending_in&=(uint8_t)~mask;
                ABoxUsbCore_Complete(n|0x80U,ABOX_USB_TRANSFER_OK,device_in_length[n]);
            }
        }
    } else {
        for (unsigned n=0U;n<8U;++n) {
            uint8_t mask=(uint8_t)(1U<<n);
            /* Some F105 revisions omit CHH when a channel halts immediately. */
            if ((host_active&mask) && !((pending_host|cancelling)&mask) &&
                !(channel(n)->HCCHAR&USB_OTG_HCCHAR_CHENA)) {
                switch (peripheral.host.hc[n].state) {
                case HC_XFRC: host_result[n]=ABOX_USB_TRANSFER_OK;pending_host|=mask;break;
                case HC_NAK: case HC_NYET: host_result[n]=ABOX_USB_TRANSFER_NAK;pending_host|=mask;break;
                case HC_STALL: host_result[n]=ABOX_USB_TRANSFER_STALL;pending_host|=mask;break;
                case HC_XACTERR: case HC_DATATGLERR: case HC_BBLERR:
                    host_result[n]=ABOX_USB_TRANSFER_ERROR;pending_host|=mask;break;
                default: break;
                }
            }
            if ((cancelling|pending_host)&mask) {
                if (channel(n)->HCCHAR&USB_OTG_HCCHAR_CHENA) {
                    if (!(halting&mask)) {
                        halting|=mask;
                        if (HAL_HCD_HC_Halt(&peripheral.host,(uint8_t)n)!=HAL_OK) {
                            host_result[n]=ABOX_USB_TRANSFER_ERROR;pending_host|=mask;
                        }
                    }
                    continue;
                }
                halting&=(uint8_t)~mask;
                if (host_errors&mask) { host_errors&=(uint8_t)~mask;pending_host|=mask;host_result[n]=ABOX_USB_TRANSFER_ERROR; }
                if (cancelling&mask) {
                    cancelling&=(uint8_t)~mask;pending_host|=mask;host_result[n]=ABOX_USB_TRANSFER_CANCELLED;
                }
            }
            if (pending_host&mask) {
                uint32_t count=host_direction[n] ? peripheral.host.hc[n].xfer_count : host_length[n];
                channel(n)->HCINT=0xffffffffU;
                pending_host&=(uint8_t)~mask;host_active&=(uint8_t)~mask;peripheral.host.hc[n].xfer_buff=NULL;
                if (host_result[n]!=ABOX_USB_TRANSFER_OK && !host_direction[n]) count=0U;
                ABoxUsbCore_Complete(n,(ABoxUsbTransferResult)host_result[n],count);
            }
        }
    }
}
ABoxHalStatus ABoxUsbHw_Open(const ABoxUsbConfig *c)
{
    HAL_StatusTypeDef status;
    if (HAL_RCC_GetHCLKFreq()<14200000U) return ABOX_HAL_UNSUPPORTED;
    /* STM32F105 lacks the later NOVBUSSENS override. */
    if (c->role==ABOX_USB_DEVICE && !c->vbus_sensing) return ABOX_HAL_UNSUPPORTED;
    memset(&peripheral,0,sizeof(peripheral));role=c->role;sof_enabled=c->sof_events;
    pending_reset=pending_setup=pending_in=pending_out=pending_errors=pending_host=host_errors=cancelling=halting=0U;
    device_cancel[0]=device_cancel[1]=0U;
    device_rx_active=host_active=0U;
    memset(device_capacity,0,sizeof(device_capacity));
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    if (role==ABOX_USB_DEVICE) {
        PCD_HandleTypeDef *h=&peripheral.device;
        h->Instance=USB_OTG_FS;h->Init.dev_endpoints=4U;h->Init.speed=PCD_SPEED_FULL;
        h->Init.phy_itface=PCD_PHY_EMBEDDED;h->Init.vbus_sensing_enable=c->vbus_sensing;
        h->Init.Sof_enable=c->sof_events;
        status=HAL_PCD_Init(h);
        if (status==HAL_OK) status=HAL_PCDEx_SetRxFiFo(h,c->rx_fifo_words);
        for (unsigned i=0U;i<4U && status==HAL_OK;++i) status=HAL_PCDEx_SetTxFiFo(h,(uint8_t)i,c->tx_fifo_words[i]);
    } else {
        HCD_HandleTypeDef *h=&peripheral.host;
        h->Instance=USB_OTG_FS;h->Init.Host_channels=8U;h->Init.speed=HCD_SPEED_FULL;
        h->Init.phy_itface=HCD_PHY_EMBEDDED;h->Init.Sof_enable=c->sof_events;
        status=HAL_HCD_Init(h);
        if (status==HAL_OK) {
            if (c->vbus_sensing) USB_OTG_FS->GCCFG|=USB_OTG_GCCFG_VBUSASEN;
            USB_OTG_FS->GRXFSIZ=c->rx_fifo_words;
            USB_OTG_FS->DIEPTXF0_HNPTXFSIZ=((uint32_t)c->tx_fifo_words[0]<<16U)|c->rx_fifo_words;
            USB_OTG_FS->HPTXFSIZ=((uint32_t)c->tx_fifo_words[1]<<16U)|(c->rx_fifo_words+c->tx_fifo_words[0]);
            if (!c->sof_events) USB_OTG_FS->GINTMSK &= ~USB_OTG_GINTMSK_SOFM;
        }
    }
    if (status!=HAL_OK) { USB_OTG_FS->GAHBCFG &= ~USB_OTG_GAHBCFG_GINT;__HAL_RCC_USB_OTG_FS_CLK_DISABLE(); }
    return result(status);
}
ABoxHalStatus ABoxUsbHw_Start(void)
{ return result(role==ABOX_USB_DEVICE ? HAL_PCD_Start(&peripheral.device) : HAL_HCD_Start(&peripheral.host)); }
ABoxHalStatus ABoxUsbHw_Stop(void)
{
    HAL_StatusTypeDef s=role==ABOX_USB_DEVICE ? HAL_PCD_Stop(&peripheral.device) : HAL_HCD_Stop(&peripheral.host);
    USB_OTG_FS->GAHBCFG &= ~USB_OTG_GAHBCFG_GINT;
    if (role==ABOX_USB_HOST) (void)USB_DriveVbus(USB_OTG_FS,0U);
    return result(s);
}
ABoxHalStatus ABoxUsbHw_Close(void)
{
    HAL_StatusTypeDef s=role==ABOX_USB_DEVICE ? HAL_PCD_DeInit(&peripheral.device) : HAL_HCD_DeInit(&peripheral.host);
    USB_OTG_FS->GAHBCFG &= ~USB_OTG_GAHBCFG_GINT;
    if (s==HAL_OK) __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
    return result(s);
}
void ABoxUsbHw_Irq(void)
{
    if(role==ABOX_USB_DEVICE) HAL_PCD_IRQHandler(&peripheral.device);else HAL_HCD_IRQHandler(&peripheral.host);
    deliver();
}
void ABoxUsbHw_Poll(void) { deliver(); }
#ifdef ABOX_USB_OTG_MANAGED
void ABoxUsbHw_OtgPause(void) { USB_OTG_FS->GAHBCFG &= ~USB_OTG_GAHBCFG_GINT; }
void ABoxUsbHw_OtgSense(void) { USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_VBUSASEN|USB_OTG_GCCFG_VBUSBSEN; }
void ABoxUsbHw_OtgResume(void) { USB_OTG_FS->GAHBCFG |= USB_OTG_GAHBCFG_GINT; }
void ABoxUsbHw_OtgAbort(void) { ABoxUsbHw_OtgPause();__HAL_RCC_USB_OTG_FS_CLK_DISABLE(); }
ABoxHalStatus ABoxUsbHw_OtgHostSignal(uint8_t suspend,uint8_t resume)
{
    volatile uint32_t *port=(volatile uint32_t *)((uintptr_t)USB_OTG_FS+USB_OTG_HOST_PORT_BASE);
    *port=ABoxUsbOtgPort_Signal(*port,suspend,resume);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_OtgAdopt(const ABoxUsbConfig *c)
{
    HAL_StatusTypeDef status;uint32_t otg_mask=USB_OTG_FS->GINTMSK & ((1UL<<2)|(1UL<<28)|(1UL<<30));
    if (!!(USB_OTG_FS->GINTSTS & 1U)!=(c->role==ABOX_USB_HOST)) return ABOX_HAL_BUSY;
    memset(&peripheral,0,sizeof(peripheral));role=c->role;sof_enabled=c->sof_events;
    pending_reset=pending_setup=pending_in=pending_out=pending_errors=pending_host=host_errors=cancelling=halting=0U;
    device_cancel[0]=device_cancel[1]=0U;device_rx_active=host_active=0U;
    memset(device_capacity,0,sizeof(device_capacity));
    if (role==ABOX_USB_DEVICE) {
        PCD_HandleTypeDef *h=&peripheral.device;
        h->Instance=USB_OTG_FS;h->Init.dev_endpoints=4U;h->Init.speed=PCD_SPEED_FULL;
        h->Init.phy_itface=PCD_PHY_EMBEDDED;h->Init.vbus_sensing_enable=c->vbus_sensing;
        h->Init.Sof_enable=c->sof_events;h->Lock=HAL_UNLOCKED;
        for(unsigned i=0U;i<4U;++i) {
            h->IN_ep[i].is_in=1U;h->IN_ep[i].num=(uint8_t)i;h->IN_ep[i].tx_fifo_num=(uint8_t)i;
            h->IN_ep[i].type=EP_TYPE_CTRL;h->OUT_ep[i].num=(uint8_t)i;h->OUT_ep[i].type=EP_TYPE_CTRL;
        }
        status=USB_DevInit(USB_OTG_FS,h->Init);
        ABoxUsbHw_OtgSense();
        if(status==HAL_OK)status=HAL_PCDEx_SetRxFiFo(h,c->rx_fifo_words);
        for(unsigned i=0U;i<4U && status==HAL_OK;++i)status=HAL_PCDEx_SetTxFiFo(h,(uint8_t)i,c->tx_fifo_words[i]);
        h->State=status==HAL_OK ? HAL_PCD_STATE_READY : HAL_PCD_STATE_ERROR;
        if(status==HAL_OK)status=USB_DevConnect(USB_OTG_FS);
    } else {
        HCD_HandleTypeDef *h=&peripheral.host;
        h->Instance=USB_OTG_FS;h->Init.Host_channels=8U;h->Init.speed=HCD_SPEED_FULL;
        h->Init.phy_itface=HCD_PHY_EMBEDDED;h->Init.Sof_enable=c->sof_events;h->Lock=HAL_UNLOCKED;
        status=USB_HostInit(USB_OTG_FS,h->Init);
        if(status==HAL_OK) {
            ABoxUsbHw_OtgSense();
            USB_OTG_FS->GRXFSIZ=c->rx_fifo_words;
            USB_OTG_FS->DIEPTXF0_HNPTXFSIZ=((uint32_t)c->tx_fifo_words[0]<<16U)|c->rx_fifo_words;
            USB_OTG_FS->HPTXFSIZ=((uint32_t)c->tx_fifo_words[1]<<16U)|(c->rx_fifo_words+c->tx_fifo_words[0]);
            if(!c->sof_events)USB_OTG_FS->GINTMSK &= ~USB_OTG_GINTMSK_SOFM;
            (void)USB_DriveVbus(USB_OTG_FS,1U); /* internal port power; board owns the external switch */
        }
        h->State=status==HAL_OK ? HAL_HCD_STATE_READY : HAL_HCD_STATE_ERROR;
    }
    USB_OTG_FS->GINTMSK|=otg_mask;
    if (!!(USB_OTG_FS->GINTSTS & 1U)!=(c->role==ABOX_USB_HOST)) return ABOX_HAL_BUSY;
    return result(status);
}
#endif
ABoxHalStatus ABoxUsbHw_DeviceOpen(const ABoxUsbDeviceEndpoint *e)
{
    HAL_StatusTypeDef s=HAL_PCD_EP_Open(&peripheral.device,e->address,e->max_packet,(uint8_t)e->type);
    if (s==HAL_OK && !(e->address&0x0fU)) {
        uint32_t encoded=e->max_packet==64U ? 0U : e->max_packet==32U ? 1U : e->max_packet==16U ? 2U : 3U;
        if (e->address&0x80U) {
            USB_OTG_INEndpointTypeDef *ep=(USB_OTG_INEndpointTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_IN_ENDPOINT_BASE);
            ep->DIEPCTL=(ep->DIEPCTL&~3U)|encoded;
        } else {
            USB_OTG_OUTEndpointTypeDef *ep=(USB_OTG_OUTEndpointTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_OUT_ENDPOINT_BASE);
            ep->DOEPCTL=(ep->DOEPCTL&~3U)|encoded;
        }
    }
    return result(s);
}
ABoxHalStatus ABoxUsbHw_DeviceClose(uint8_t address) { return result(HAL_PCD_EP_Close(&peripheral.device,address)); }
ABoxHalStatus ABoxUsbHw_DeviceTransfer(uint8_t address,uint8_t *buffer,uint16_t length)
{
    HAL_StatusTypeDef status;
    if (!(address&0x80U)) {
        unsigned n=address&15U;uint32_t packet=peripheral.device.OUT_ep[n].maxpacket;
        device_capacity[n]=length ? ((length+packet-1U)/packet)*packet : packet;
        device_rx_active|=(uint8_t)(1U<<n);
    } else device_in_length[address&15U]=length;
    status=address&0x80U ? HAL_PCD_EP_Transmit(&peripheral.device,address,buffer,length) :
                           HAL_PCD_EP_Receive(&peripheral.device,address,buffer,length);
    if(status!=HAL_OK && !(address&0x80U))device_rx_active&=(uint8_t)~(1U<<(address&15U));
    return result(status);
}
ABoxHalStatus ABoxUsbHw_DeviceCancel(uint8_t address)
{
    unsigned n=address&15U,dir=address>>7U;uint8_t mask=(uint8_t)(1U<<n);
    volatile uint32_t *ctl=dir ? &((USB_OTG_INEndpointTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_IN_ENDPOINT_BASE+n*USB_OTG_EP_REG_SIZE))->DIEPCTL :
                                  &((USB_OTG_OUTEndpointTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_OUT_ENDPOINT_BASE+n*USB_OTG_EP_REG_SIZE))->DOEPCTL;
    if (USB_OTG_FS->GINTSTS&USB_OTG_GINTSTS_RXFLVL) return ABOX_HAL_BUSY;
    device_cancel[dir]|=mask;
    if(dir) ((USB_OTG_DeviceTypeDef *)((uintptr_t)USB_OTG_FS+USB_OTG_DEVICE_BASE))->DIEPEMPMSK&=~mask;
    if(*ctl&(1UL<<31U))*ctl|=(1UL<<27U)|(1UL<<30U);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_DeviceStall(uint8_t address,uint8_t stall)
{ return result(stall ? HAL_PCD_EP_SetStall(&peripheral.device,address) : HAL_PCD_EP_ClrStall(&peripheral.device,address)); }
ABoxHalStatus ABoxUsbHw_DeviceAddress(uint8_t address) { return result(HAL_PCD_SetAddress(&peripheral.device,address)); }
ABoxHalStatus ABoxUsbHw_HostReset(void) { return result(HAL_HCD_ResetPort(&peripheral.host)); }
ABoxHalStatus ABoxUsbHw_HostOpen(unsigned n,const ABoxUsbHostPipe *p)
{
    host_direction[n]=(p->address&0x80U)!=0U;
    return result(HAL_HCD_HC_Init(&peripheral.host,(uint8_t)n,p->address,p->device_address,
        p->speed==ABOX_USB_LOW_SPEED ? HCD_DEVICE_SPEED_LOW : HCD_DEVICE_SPEED_FULL,(uint8_t)p->type,p->max_packet));
}
ABoxHalStatus ABoxUsbHw_HostClose(unsigned n)
{
    channel(n)->HCINTMSK=0U;channel(n)->HCINT=0xffffffffU;host_registers()->HAINTMSK &= ~(1U<<n);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_HostSubmit(unsigned n,ABoxUsbPid pid,uint8_t *buffer,uint16_t length)
{
    USB_OTG_HCTypeDef *hc=&peripheral.host.hc[n];
    uint32_t fifo=(hc->ep_type==EP_TYPE_INTR || hc->ep_type==EP_TYPE_ISOC) ?
                  host_registers()->HPTXSTS : USB_OTG_FS->HNPTXSTS;
    /* The vendor FS driver writes the packet directly to the FIFO. Avoid
     * submitting until both queue and the whole packet fit. */
    if (!(fifo&0x00ff0000U) || (!hc->ep_is_in && (fifo&0xffffU)<(length+3U)/4U)) return ABOX_HAL_BUSY;
    hc->data_pid=pid==ABOX_USB_SETUP ? HC_PID_SETUP : pid==ABOX_USB_DATA1 ? HC_PID_DATA1 : HC_PID_DATA0;
    hc->xfer_buff=buffer;hc->xfer_len=host_direction[n] ? hc->max_packet : length;
    hc->xfer_count=0U;hc->ch_num=(uint8_t)n;hc->state=HC_IDLE;hc->urb_state=URB_IDLE;
    host_length[n]=length;host_active|=(uint8_t)(1U<<n);
    {
        HAL_StatusTypeDef status=USB_HC_StartXfer(USB_OTG_FS,hc);
        if(status!=HAL_OK)host_active&=(uint8_t)~(1U<<n);
        return result(status);
    }
}
ABoxHalStatus ABoxUsbHw_HostHalt(unsigned n)
{
    if (USB_OTG_FS->GINTSTS & USB_OTG_GINTSTS_RXFLVL) return ABOX_HAL_BUSY;
    cancelling|=(uint8_t)(1U<<n);
    if(channel(n)->HCCHAR&USB_OTG_HCCHAR_CHENA) {
        halting|=(uint8_t)(1U<<n);
        if(HAL_HCD_HC_Halt(&peripheral.host,(uint8_t)n)!=HAL_OK) { host_errors|=(uint8_t)(1U<<n); }
    }
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_HostSpeed(ABoxUsbSpeed *speed)
{
    uint32_t s=HAL_HCD_GetCurrentSpeed(&peripheral.host);
    if(s==HCD_DEVICE_SPEED_FULL)*speed=ABOX_USB_FULL_SPEED;
    else if(s==HCD_DEVICE_SPEED_LOW)*speed=ABOX_USB_LOW_SPEED;else return ABOX_HAL_UNSUPPORTED;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbHw_HostFrame(uint16_t *frame) { *frame=(uint16_t)HAL_HCD_GetCurrentFrame(&peripheral.host);return ABOX_HAL_OK; }
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *h) { (void)h;pending_setup=1U; }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *h,uint8_t n) { (void)h;if(n<4U)pending_out|=(uint8_t)(1U<<n); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *h,uint8_t n) { (void)h;if(n<4U)pending_in|=(uint8_t)(1U<<n); }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *h)
{ (void)h;pending_setup=pending_in=pending_out=pending_errors=device_rx_active=0U;device_cancel[0]=device_cancel[1]=0U;memset(device_capacity,0,sizeof(device_capacity));pending_reset=1U; }
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_SUSPEND,0U); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_RESUME,0U); }
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_CONNECTED,0U); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_DISCONNECTED,0U); }
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *h) { (void)h;if(sof_enabled)ABoxUsbCore_Event(ABOX_USB_SOF,0U); }
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *h,uint8_t n) { (void)h;ABoxUsbCore_Event(ABOX_USB_ISO_INCOMPLETE,n|0x80U); }
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *h,uint8_t n) { (void)h;ABoxUsbCore_Event(ABOX_USB_ISO_INCOMPLETE,n); }
void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_CONNECTED,0U); }
void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_DISCONNECTED,0U); }
void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_PORT_ENABLED,0U); }
void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *h) { (void)h;ABoxUsbCore_Event(ABOX_USB_PORT_DISABLED,0U); }
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *h) { (void)h;if(sof_enabled)ABoxUsbCore_Event(ABOX_USB_SOF,0U); }
void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *h,uint8_t n,HCD_URBStateTypeDef state)
{
    (void)h;if(n>=8U || state==URB_IDLE || !(host_active&(1U<<n)))return;
    host_result[n]=(uint8_t)(state==URB_DONE ? ABOX_USB_TRANSFER_OK : state==URB_STALL ? ABOX_USB_TRANSFER_STALL :
                             state==URB_NOTREADY || state==URB_NYET ? ABOX_USB_TRANSFER_NAK : ABOX_USB_TRANSFER_ERROR);
    pending_host|=(uint8_t)(1U<<n);
}
