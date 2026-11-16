#include "abox_usb_backend.h"
#include "abox_critical.h"
#include "abox_platform_port.h"
#include <string.h>
#ifdef ABOX_USB_OTG_MANAGED
#include "abox_usb_otg.h"
#include "abox_usb_otg_backend.h"
#endif

typedef struct { uint16_t max_packet,length;uint8_t open,active,type; } Endpoint;
typedef struct { ABoxUsbHostPipe config;uint16_t length;uint8_t open,active; } Pipe;
static struct {
    ABoxUsbConfig config;const ABoxUsbEvents *events;
    Endpoint device[2][4];Pipe host[8];
    volatile uint8_t open,started;
    uint8_t reinit; /* 1: reopen; 2: close after failed stop; 3: OTG hardware already aborted */
#ifdef ABOX_USB_OTG_MANAGED
    ABoxUsbConfig alternate;
    volatile uint8_t managed,paused,switching,host_suspended,resuming;
#endif
} usb;
static unsigned ep_direction(uint8_t address) { return (address>>7U)&1U; }
static int valid_address(uint8_t address) { return !(address&0x70U) && (address&0x0fU)<4U; }
static int packet_valid(ABoxUsbSpeed speed,ABoxUsbEndpointType type,uint16_t size)
{
    if ((unsigned)speed>ABOX_USB_LOW_SPEED || (unsigned)type>ABOX_USB_INTERRUPT || !size) return 0;
    if (speed==ABOX_USB_LOW_SPEED)
        return type==ABOX_USB_CONTROL ? size==8U : type==ABOX_USB_INTERRUPT && size<=8U;
    if (type==ABOX_USB_CONTROL || type==ABOX_USB_BULK) return size==8U || size==16U || size==32U || size==64U;
    return size<=(type==ABOX_USB_INTERRUPT ? 64U : 1023U);
}
static uint32_t rounded(uint16_t length,uint16_t packet)
{ return length ? (((uint32_t)length+packet-1U)/packet)*packet : packet; }
static uint32_t aligned_words(uint32_t length) { return (length+3U)&~3U; }
static int started_as(ABoxUsbRole role)
{
#ifdef ABOX_USB_OTG_MANAGED
    ABoxUsbOtgStatus s;
    if (usb.managed && (usb.paused || usb.switching || ABoxUsbOtg_GetStatus(&s)!=ABOX_HAL_OK ||
        s.host!=(role==ABOX_USB_HOST))) return 0;
#endif
    return usb.open && usb.started && usb.config.role==role;
}
static int device_rx_fifo_valid(const ABoxUsbDeviceEndpoint *candidate)
{
    uint32_t words=(candidate->max_packet+3U)/4U;
    unsigned iso=candidate->type==ABOX_USB_ISOCHRONOUS;
    for(unsigned n=0U;n<4U;++n) if(usb.device[0][n].open) {
        uint32_t other=(usb.device[0][n].max_packet+3U)/4U;
        if(other>words)words=other;
        if(usb.device[0][n].type==ABOX_USB_ISOCHRONOUS)++iso;
    }
    /* 10 SETUP, one global NAK and up to four OUT-complete statuses. */
    return usb.config.rx_fifo_words>=15U+(words+1U)*(iso>1U ? 2U : 1U);
}
static int host_rx_fifo_valid(const ABoxUsbHostPipe *candidate)
{
    uint32_t words=(candidate->max_packet+3U)/4U,required;
    unsigned iso=candidate->type==ABOX_USB_ISOCHRONOUS;
    for(unsigned n=0U;n<8U;++n) if(usb.host[n].open && (usb.host[n].config.address&0x80U)) {
        uint32_t other=(usb.host[n].config.max_packet+3U)/4U;
        if(other>words)words=other;
        if(usb.host[n].config.type==ABOX_USB_ISOCHRONOUS)++iso;
    }
    required=words+18U; /* Includes F105 host receive FIFO erratum margin. */
    if(iso>1U && required<2U*(words+1U)+8U)required=2U*(words+1U)+8U;
    return usb.config.rx_fifo_words>=required;
}
static int config_valid(const ABoxUsbConfig *config)
{
    uint32_t total;
    if (!config || (unsigned)config->role>ABOX_USB_HOST ||
        config->usb_clock_hz!=48000000U || config->vbus_sensing>1U || config->sof_events>1U ||
        config->rx_fifo_words<32U || config->tx_fifo_words[0]<16U) return 0;
    total=config->rx_fifo_words;
    for (unsigned i=0U;i<4U;++i) {
        if (config->tx_fifo_words[i] && config->tx_fifo_words[i]<16U) return 0;
        if (i && config->tx_fifo_words[i] && !config->tx_fifo_words[i-1U]) return 0;
        total+=config->tx_fifo_words[i];
    }
    if (total>320U || (config->role==ABOX_USB_HOST &&
        (config->tx_fifo_words[1]<16U || config->tx_fifo_words[2] || config->tx_fifo_words[3]))) return 0;
    return 1;
}
ABoxHalStatus ABoxUsb_Open(const ABoxUsbConfig *config,const ABoxUsbEvents *events)
{
    ABoxHalStatus status;const ABoxPlatformPort *port=ABox_PlatformPortGet();
    if (!events || !port || !port->get_tick_ms || !config_valid(config)) return ABOX_HAL_INVALID;
    if (usb.open) return ABOX_HAL_BUSY;
    memset(&usb,0,sizeof(usb));usb.config=*config;usb.events=events;
    status=ABoxUsbHw_Open(config);
    if (status==ABOX_HAL_OK) usb.open=1U;else usb.events=0;
    return status;
}
ABoxHalStatus ABoxUsb_Start(void)
{
    ABoxHalStatus status;uint32_t key;
    if (!usb.open) return ABOX_HAL_INVALID;
    if (usb.started) return ABOX_HAL_BUSY;
    if (usb.reinit>=2U) return ABOX_HAL_ERROR;
    if (usb.reinit==1U) {
        status=ABoxUsbHw_Close();if(status!=ABOX_HAL_OK)return status;
        status=ABoxUsbHw_Open(&usb.config);
        if(status!=ABOX_HAL_OK) { usb.open=0U;usb.events=0;return status; }
        usb.reinit=0U;
    }
    key=ABoxCritical_Enter();usb.started=1U;ABoxCritical_Exit(key);
    status=ABoxUsbHw_Start();
    if (status!=ABOX_HAL_OK) { key=ABoxCritical_Enter();usb.started=0U;ABoxCritical_Exit(key); }
    return status;
}
ABoxHalStatus ABoxUsb_Stop(void)
{
    ABoxHalStatus status;uint32_t key;
    if (!usb.open || !usb.started) return ABOX_HAL_INVALID;
#ifdef ABOX_USB_OTG_MANAGED
    if (usb.managed && !started_as(usb.config.role)) return ABOX_HAL_BUSY;
#endif
    for (unsigned dir=0U;dir<2U;++dir)
        for (unsigned n=0U;n<4U;++n) if (usb.device[dir][n].active) return ABOX_HAL_BUSY;
    for (unsigned n=0U;n<8U;++n) if (usb.host[n].active) return ABOX_HAL_BUSY;
#ifdef ABOX_USB_OTG_MANAGED
    if (usb.managed) {
        ABoxUsbOtgCore_DisableKeepRole(usb.config.role==ABOX_USB_HOST);
        usb.managed=usb.host_suspended=usb.resuming=0U;
    }
#endif
    key=ABoxCritical_Enter();usb.started=0U;ABoxCritical_Exit(key);
    status=ABoxUsbHw_Stop();
    memset(usb.device,0,sizeof(usb.device));memset(usb.host,0,sizeof(usb.host));
    usb.reinit=status==ABOX_HAL_OK ? 1U : 2U;
    return status;
}
ABoxHalStatus ABoxUsb_Close(void)
{
    ABoxHalStatus status;
    if (!usb.open) return ABOX_HAL_INVALID;
    if (usb.started) return ABOX_HAL_BUSY;
    if (usb.reinit==3U) { usb.open=0U;usb.events=0;return ABOX_HAL_OK; }
    status=ABoxUsbHw_Close();if (status==ABOX_HAL_OK) { usb.open=0U;usb.events=0; }return status;
}
#ifdef ABOX_USB_OTG_MANAGED
ABoxHalStatus ABoxUsbOtg_EnableManaged(const ABoxUsbConfig *alternate,const ABoxUsbOtgEvents *events)
{
    ABoxHalStatus result;uint32_t key;
    if (!usb.open || !usb.started || !config_valid(alternate) || alternate->role==usb.config.role ||
        alternate->usb_clock_hz!=usb.config.usb_clock_hz || !alternate->vbus_sensing || !usb.config.vbus_sensing)
        return ABOX_HAL_INVALID;
    if (usb.managed) return ABOX_HAL_BUSY;
    key=ABoxCritical_Enter();usb.alternate=*alternate;usb.managed=1U;
    usb.paused=usb.switching=usb.host_suspended=usb.resuming=0U;
    result=ABoxUsbOtg_Enable(events);
    if(result!=ABOX_HAL_OK)usb.managed=0U;else ABoxUsbHw_OtgSense();
    ABoxCritical_Exit(key);return result;
}
static int otg_role(ABoxUsbRole *role)
{
    ABoxUsbOtgStatus s;
    if (ABoxUsbOtg_GetStatus(&s)!=ABOX_HAL_OK) return 0;
    *role=s.host ? ABOX_USB_HOST : ABOX_USB_DEVICE;return 1;
}
int ABoxUsbOtgCore_IsAdopting(void) { return usb.managed && usb.switching; }
ABoxHalStatus ABoxUsbOtg_HostSuspend(void)
{
    ABoxHalStatus result;
    if (!usb.managed || !started_as(ABOX_USB_HOST)) return ABOX_HAL_INVALID;
    if (usb.host_suspended) return ABOX_HAL_BUSY;
    for(unsigned i=0U;i<8U;++i)if(usb.host[i].active)return ABOX_HAL_BUSY;
    result=ABoxUsbHw_OtgHostSignal(1U,0U);if(result==ABOX_HAL_OK)usb.host_suspended=1U;return result;
}
ABoxHalStatus ABoxUsbOtg_HostResume(uint8_t asserted)
{
    ABoxHalStatus result;
    if (!usb.managed || !started_as(ABOX_USB_HOST) || asserted>1U)return ABOX_HAL_INVALID;
    if(!usb.host_suspended || (asserted && usb.resuming) || (!asserted && !usb.resuming))return ABOX_HAL_INVALID;
    result=ABoxUsbHw_OtgHostSignal(0U,asserted);
    if(result==ABOX_HAL_OK){usb.resuming=asserted;if(!asserted)usb.host_suspended=0U;}
    return result;
}
static void otg_pause(void)
{ if(!usb.paused){ABoxUsbHw_OtgPause();usb.paused=1U;} }
static void otg_poll(void)
{
    ABoxUsbRole current;ABoxUsbConfig old;ABoxHalStatus result;
    if (!usb.managed || usb.switching || !otg_role(&current)) return;
    if (current==usb.config.role) {
        if (usb.paused) { usb.paused=0U;ABoxUsbHw_OtgResume(); }
        return;
    }
    otg_pause();usb.switching=1U;old=usb.config;
    result=ABoxUsbHw_OtgAdopt(&usb.alternate);
    /* Hardware FIFO traffic is stopped before ownership returns to callers. */
    if (old.role==ABOX_USB_DEVICE) {
        for (unsigned dir=0U;dir<2U;++dir) for (unsigned n=0U;n<4U;++n)
            if (usb.device[dir][n].active) ABoxUsbCore_Complete(n|(dir ? 0x80U : 0U),ABOX_USB_TRANSFER_CANCELLED,0U);
    } else for (unsigned n=0U;n<8U;++n)
        if (usb.host[n].active) ABoxUsbCore_Complete(n,ABOX_USB_TRANSFER_CANCELLED,0U);
    memset(usb.device,0,sizeof(usb.device));memset(usb.host,0,sizeof(usb.host));
    usb.config=usb.alternate;usb.alternate=old;usb.host_suspended=usb.resuming=0U;usb.switching=0U;
    if (result!=ABOX_HAL_OK) {
        const ABoxUsbEvents *e=usb.events;
        (void)otg_role(&current);
        ABoxUsbOtgCore_DisableKeepRole(current==ABOX_USB_HOST);ABoxUsbHw_OtgAbort();
        usb.managed=0U;usb.started=0U;usb.reinit=3U;
        if(e && e->event)e->event(e->context,ABOX_USB_ROLE_ERROR,(unsigned)result);
        return;
    }
    ABoxUsbCore_Event(ABOX_USB_ROLE_CHANGED,(unsigned)current);
    if (otg_role(&current) && current==usb.config.role) { usb.paused=0U;ABoxUsbHw_OtgResume(); }
}
#endif
void ABoxUsb_Irq(void)
{
    if (!usb.open || !usb.started) return;
#ifdef ABOX_USB_OTG_MANAGED
    if (usb.managed) {
        ABoxUsbRole current;
        ABoxUsbOtg_Irq();
        if (!otg_role(&current) || current!=usb.config.role || usb.switching) { otg_pause();return; }
    }
#endif
    ABoxUsbHw_Irq();
}
void ABoxUsb_Poll(void)
{
    uint32_t key;
#ifdef ABOX_USB_OTG_MANAGED
    if (usb.open && usb.started && usb.managed) otg_poll();
#endif
    key=ABoxCritical_Enter();
#ifdef ABOX_USB_OTG_MANAGED
    if (usb.managed && (usb.paused || usb.switching)) { ABoxCritical_Exit(key);return; }
#endif
    if (usb.open && usb.started) ABoxUsbHw_Poll();
    ABoxCritical_Exit(key);
}
void ABoxUsbCore_Event(ABoxUsbEvent event,unsigned number)
{
    const ABoxUsbEvents *e=usb.events;
    if (!usb.open || !usb.started) return;
    if (event==ABOX_USB_RESET && usb.config.role==ABOX_USB_DEVICE) {
        for (unsigned dir=0U;dir<2U;++dir) for (unsigned n=0U;n<4U;++n)
            if (usb.device[dir][n].active) ABoxUsbCore_Complete(n|(dir ? 0x80U : 0U),ABOX_USB_TRANSFER_CANCELLED,0U);
        memset(usb.device,0,sizeof(usb.device));
    }
    if (e && e->event) e->event(e->context,event,number);
}
void ABoxUsbCore_Setup(const uint8_t setup[8])
{
    const ABoxUsbEvents *e=usb.events;
    if (started_as(ABOX_USB_DEVICE) && setup && e && e->setup) e->setup(e->context,setup);
}
void ABoxUsbCore_Complete(unsigned number,ABoxUsbTransferResult result,uint32_t count)
{
    const ABoxUsbEvents *e=usb.events;uint16_t length;
    if (!usb.open || !usb.started) return;
    if (usb.config.role==ABOX_USB_DEVICE) {
        Endpoint *ep;
        if (number>255U || !valid_address((uint8_t)number)) return;
        ep=&usb.device[ep_direction((uint8_t)number)][number&0x0fU];
        if (!ep->active) return;
        ep->active=0U;length=ep->length;
    } else {
        Pipe *pipe;if (number>=8U) return;
        pipe=&usb.host[number];if (!pipe->active) return;
        pipe->active=0U;length=pipe->length;
    }
    if (count>length) result=ABOX_USB_TRANSFER_ERROR;
    if (e && e->complete) e->complete(e->context,number,result,count);
}
ABoxHalStatus ABoxUsbDevice_OpenEndpoint(const ABoxUsbDeviceEndpoint *endpoint)
{
    Endpoint *e;ABoxHalStatus status;unsigned n,dir;uint32_t key;
    if (!started_as(ABOX_USB_DEVICE) || !endpoint || !valid_address(endpoint->address) ||
        !packet_valid(ABOX_USB_FULL_SPEED,endpoint->type,endpoint->max_packet)) return ABOX_HAL_INVALID;
    n=endpoint->address&0x0fU;dir=ep_direction(endpoint->address);
    if (!n && endpoint->type!=ABOX_USB_CONTROL) return ABOX_HAL_INVALID;
    if (dir && usb.config.tx_fifo_words[n] < (endpoint->max_packet+3U)/4U) return ABOX_HAL_UNSUPPORTED;
    key=ABoxCritical_Enter();e=&usb.device[dir][n];
    if(e->open){ABoxCritical_Exit(key);return ABOX_HAL_BUSY;}
    if(!dir && !device_rx_fifo_valid(endpoint)){ABoxCritical_Exit(key);return ABOX_HAL_UNSUPPORTED;}
    status=ABoxUsbHw_DeviceOpen(endpoint);
    if(status==ABOX_HAL_OK){e->open=1U;e->max_packet=endpoint->max_packet;e->type=(uint8_t)endpoint->type;}
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxUsbDevice_CloseEndpoint(uint8_t address)
{
    Endpoint *e;ABoxHalStatus status;uint32_t key;
    if (!started_as(ABOX_USB_DEVICE) || !valid_address(address)) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();e=&usb.device[ep_direction(address)][address&0x0fU];
    if (!e->open)status=ABOX_HAL_INVALID;
    else if (e->active)status=ABOX_HAL_BUSY;
    else { status=ABoxUsbHw_DeviceClose(address);if(status==ABOX_HAL_OK)e->open=0U; }
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxUsbDevice_Transfer(uint8_t address,uint8_t *buffer,uint16_t length,uint16_t capacity)
{
    Endpoint *e;ABoxHalStatus status;uint32_t key,required;
    if (!started_as(ABOX_USB_DEVICE) || !valid_address(address)) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();e=&usb.device[ep_direction(address)][address&0x0fU];
    if (!e->open) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    if (((!(address&0x0fU) || e->type==ABOX_USB_ISOCHRONOUS) && length>e->max_packet) ||
        (length && ((uint32_t)length+e->max_packet-1U)/e->max_packet>1023U)) {
        ABoxCritical_Exit(key);return ABOX_HAL_UNSUPPORTED;
    }
    required=aligned_words(ep_direction(address) ? length : rounded(length,e->max_packet));
    if (capacity<required || (required && !buffer)) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    if(e->active){ABoxCritical_Exit(key);return ABOX_HAL_BUSY;}
    e->active=1U;e->length=length;status=ABoxUsbHw_DeviceTransfer(address,buffer,length);
    if(status!=ABOX_HAL_OK)e->active=0U;
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxUsbDevice_Cancel(uint8_t address)
{
    ABoxHalStatus status;uint32_t key=ABoxCritical_Enter();
    if (!started_as(ABOX_USB_DEVICE) || !valid_address(address) ||
        !usb.device[ep_direction(address)][address&15U].active) status=ABOX_HAL_INVALID;
    else status=ABoxUsbHw_DeviceCancel(address);
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxUsbDevice_SetStall(uint8_t address,uint8_t stall)
{
    Endpoint *e;ABoxHalStatus status;uint32_t key;
    if (!started_as(ABOX_USB_DEVICE) || !valid_address(address) || stall>1U) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();e=&usb.device[ep_direction(address)][address&0x0fU];
    if(!e->open)status=ABOX_HAL_INVALID;
    else if(e->active)status=ABOX_HAL_BUSY;
    else status=ABoxUsbHw_DeviceStall(address,stall);
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxUsbDevice_SetAddress(uint8_t address)
{ if(!started_as(ABOX_USB_DEVICE) || address>127U)return ABOX_HAL_INVALID;return ABoxUsbHw_DeviceAddress(address); }
ABoxHalStatus ABoxUsbDevice_RemoteWakeup(uint8_t asserted)
{ if(!started_as(ABOX_USB_DEVICE) || asserted>1U)return ABOX_HAL_INVALID;return ABoxUsbHw_DeviceRemoteWakeup(asserted); }
ABoxHalStatus ABoxUsbHost_ResetPort(void)
{ if(!started_as(ABOX_USB_HOST))return ABOX_HAL_INVALID;return ABoxUsbHw_HostReset(); }
ABoxHalStatus ABoxUsbHost_OpenPipe(unsigned channel,const ABoxUsbHostPipe *pipe)
{
    ABoxHalStatus status;uint32_t words;
    if(!started_as(ABOX_USB_HOST) || channel>=8U || !pipe || (pipe->address&0x70U) ||
       pipe->device_address>127U || !packet_valid(pipe->speed,pipe->type,pipe->max_packet) ||
       (!(pipe->address&0x0fU) && pipe->type!=ABOX_USB_CONTROL))return ABOX_HAL_INVALID;
    words=(pipe->max_packet+3U)/4U;
    if ((pipe->address&0x80U) && (!host_rx_fifo_valid(pipe) ||
        (usb.config.tx_fifo_words[0]<words && usb.config.tx_fifo_words[1]<words))) return ABOX_HAL_UNSUPPORTED;
    if (!(pipe->address&0x80U) && usb.config.tx_fifo_words[pipe->type==ABOX_USB_INTERRUPT || pipe->type==ABOX_USB_ISOCHRONOUS ? 1U : 0U]<
        words*(pipe->type==ABOX_USB_ISOCHRONOUS ? 2U : 1U))
        return ABOX_HAL_UNSUPPORTED;
    if(usb.host[channel].open)return ABOX_HAL_BUSY;
    status=ABoxUsbHw_HostOpen(channel,pipe);
    if(status==ABOX_HAL_OK){usb.host[channel].open=1U;usb.host[channel].config=*pipe;}
    return status;
}
ABoxHalStatus ABoxUsbHost_ClosePipe(unsigned channel)
{
    ABoxHalStatus status;Pipe *p;
    if(!started_as(ABOX_USB_HOST) || channel>=8U || !usb.host[channel].open)return ABOX_HAL_INVALID;
    p=&usb.host[channel];if(p->active)return ABOX_HAL_BUSY;
    status=ABoxUsbHw_HostClose(channel);if(status==ABOX_HAL_OK)p->open=0U;return status;
}
ABoxHalStatus ABoxUsbHost_Submit(unsigned channel,ABoxUsbPid pid,uint8_t *buffer,uint16_t length,uint16_t capacity)
{
    Pipe *p;ABoxHalStatus status;uint32_t required,key;
#ifdef ABOX_USB_OTG_MANAGED
    if (usb.managed && usb.host_suspended) return ABOX_HAL_BUSY;
#endif
    if(!started_as(ABOX_USB_HOST) || channel>=8U || !usb.host[channel].open || (unsigned)pid>ABOX_USB_DATA1)return ABOX_HAL_INVALID;
    p=&usb.host[channel];
    /* Both vendors' FS FIFO drivers schedule one bounded packet per submit. */
    if(length>p->config.max_packet)return ABOX_HAL_UNSUPPORTED;
    if(pid==ABOX_USB_SETUP && (p->config.type!=ABOX_USB_CONTROL || (p->config.address&0x80U) || length!=8U))return ABOX_HAL_INVALID;
    required=aligned_words((p->config.address&0x80U) ? rounded(length,p->config.max_packet) : length);
    if(capacity<required || (required && !buffer))return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();if(p->active){ABoxCritical_Exit(key);return ABOX_HAL_BUSY;}
    p->active=1U;p->length=length;status=ABoxUsbHw_HostSubmit(channel,pid,buffer,length);
    if(status!=ABOX_HAL_OK)p->active=0U;
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxUsbHost_Halt(unsigned channel)
{
    ABoxHalStatus status;uint32_t key=ABoxCritical_Enter();
    if(!started_as(ABOX_USB_HOST)||channel>=8U||!usb.host[channel].active)status=ABOX_HAL_INVALID;
    else status=ABoxUsbHw_HostHalt(channel);
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxUsbHost_PortSpeed(ABoxUsbSpeed *speed)
{ if(!started_as(ABOX_USB_HOST)||!speed)return ABOX_HAL_INVALID;return ABoxUsbHw_HostSpeed(speed); }
ABoxHalStatus ABoxUsbHost_Frame(uint16_t *frame)
{ if(!started_as(ABOX_USB_HOST)||!frame)return ABOX_HAL_INVALID;return ABoxUsbHw_HostFrame(frame); }
