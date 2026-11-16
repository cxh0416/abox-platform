#include "abox_usb_otg.h"
#include "abox_usb_otg_backend.h"
#define FORCE_MODE ((1UL<<29)|(1UL<<30))
#define CAPS ((1UL<<8)|(1UL<<9))
#define IRQ_OTG (1UL<<2)
#define GLOBAL_EVENTS ((1UL<<28)|(1UL<<30))
#define OTG_EVENTS ((1UL<<2)|(1UL<<8)|(1UL<<9)|(1UL<<17)|(1UL<<18)|(1UL<<19))
#define MODE_HOST 1UL
#define ID_B (1UL<<16)
#define B_VALID (1UL<<19)
#define HNP_REQUEST (1UL<<9)
#define HNP_HOST_ENABLE (1UL<<10)
#define HNP_DEVICE_ENABLE (1UL<<11)
#define SRP_REQUEST (1UL<<1)
static const ABoxUsbOtgEvents *listener;
static uint32_t saved_config,saved_mask;
static void status_read(const ABoxUsbOtgRegisters *r,ABoxUsbOtgStatus *out)
{
    uint32_t ctrl=r->control;out->host=(uint8_t)!!(r->global_status & MODE_HOST);
    out->b_device=(uint8_t)!!(ctrl & ID_B);out->a_session_valid=out->host ? (uint8_t)!!(ctrl & (1UL<<18)) : 0U;
    out->b_session_valid=out->host ? 0U : (uint8_t)!!(ctrl & B_VALID);
    out->srp_success=out->host ? 0U : (uint8_t)!!(ctrl & 1U);
    out->hnp_success=out->host ? 0U : (uint8_t)!!(ctrl & (1UL<<8));
}
ABoxHalStatus ABoxUsbOtg_Enable(const ABoxUsbOtgEvents *events)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();
    if (!events || !events->event || !r) return ABOX_HAL_INVALID;
    if (listener) return ABOX_HAL_BUSY;
    saved_config=r->usb_config & (FORCE_MODE|CAPS);saved_mask=r->global_mask & (IRQ_OTG|GLOBAL_EVENTS);
    listener=events;r->usb_config=(r->usb_config & ~FORCE_MODE)|CAPS;
    r->global_mask |= IRQ_OTG|GLOBAL_EVENTS;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbOtg_Disable(void)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();
    if (!listener || !r) return ABOX_HAL_INVALID;
    r->global_mask=(r->global_mask & ~(IRQ_OTG|GLOBAL_EVENTS))|saved_mask;
    r->usb_config=(r->usb_config & ~(FORCE_MODE|CAPS))|saved_config;
    listener=0;return ABOX_HAL_OK;
}
void ABoxUsbOtgCore_DisableKeepRole(uint8_t host)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();
    if (!listener || !r) return;
    r->global_mask=(r->global_mask & ~(IRQ_OTG|GLOBAL_EVENTS))|saved_mask;
    r->usb_config=(r->usb_config & ~(FORCE_MODE|CAPS))|(saved_config&CAPS)|
                  (host ? (1UL<<29) : (1UL<<30));
    listener=0;
}
ABoxHalStatus ABoxUsbOtg_GetStatus(ABoxUsbOtgStatus *status)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();
    if (!listener || !status || !r) return ABOX_HAL_INVALID;
    status_read(r,status);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbOtg_SetHnpPermission(uint8_t enabled)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();uint32_t bit;
    if (!listener || !r || enabled>1U) return ABOX_HAL_INVALID;
    bit=(r->global_status & MODE_HOST) ? HNP_HOST_ENABLE : HNP_DEVICE_ENABLE;
    if (enabled) r->control |= bit;else r->control &= ~bit;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbOtg_RequestHnp(void)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();
    if (!listener || !r) return ABOX_HAL_INVALID;
    if ((r->global_status & MODE_HOST) || !(r->control & ID_B) || !(r->control & HNP_DEVICE_ENABLE)) return ABOX_HAL_BUSY;
    r->control |= HNP_REQUEST;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxUsbOtg_RequestSrp(void)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();
    if (!listener || !r) return ABOX_HAL_INVALID;
    if ((r->global_status & MODE_HOST) || !(r->control & ID_B) || (r->control & B_VALID)) return ABOX_HAL_BUSY;
    r->control |= SRP_REQUEST;return ABOX_HAL_OK;
}
void ABoxUsbOtg_Irq(void)
{
    ABoxUsbOtgRegisters *r=ABoxUsbOtgHw_Registers();const ABoxUsbOtgEvents *e=listener;
    ABoxUsbOtgStatus status;uint32_t pending,events=0U;
    if (!e || !r) return;
    pending=r->global_status & r->global_mask;
    status_read(r,&status);
    if (pending & IRQ_OTG) { events=r->otg_interrupt & OTG_EVENTS;if(events)r->otg_interrupt=events; }
    events |= pending & GLOBAL_EVENTS;
    if (pending & GLOBAL_EVENTS) r->global_status=pending & GLOBAL_EVENTS;
    if (events) e->event(e->context,events,&status);
}
