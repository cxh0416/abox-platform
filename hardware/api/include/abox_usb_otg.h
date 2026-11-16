#ifndef ABOX_USB_OTG_H
#define ABOX_USB_OTG_H
#include <stdint.h>
#include "abox_hal_status.h"
#include "abox_usb.h"

/* OTG controller signalling events. Multiple conditions can be coalesced. */
#define ABOX_USB_OTG_SESSION_END    (1UL<<2)
#define ABOX_USB_OTG_SRP_COMPLETE   (1UL<<8)
#define ABOX_USB_OTG_HNP_COMPLETE   (1UL<<9)
#define ABOX_USB_OTG_HNP_DETECTED   (1UL<<17)
#define ABOX_USB_OTG_A_TIMEOUT      (1UL<<18)
#define ABOX_USB_OTG_DEBOUNCE       (1UL<<19)
#define ABOX_USB_OTG_ID_CHANGE      (1UL<<28)
#define ABOX_USB_OTG_SESSION_REQUEST (1UL<<30)
typedef struct {
    uint8_t host,b_device,a_session_valid,b_session_valid; /* A valid in host, B in device mode */
    uint8_t srp_success,hnp_success; /* success fields valid in device mode */
} ABoxUsbOtgStatus;
typedef struct {
    void *context;
    void (*event)(void *context,uint32_t events,const ABoxUsbOtgStatus *status);
} ABoxUsbOtgEvents;
/* Standalone signalling primitives for an initialized F105 USB OTG core.
 * Enable selects automatic role mode and HNP/SRP capability; a caller using
 * these standalone calls owns role-specific data-path setup and IRQ dispatch.
 * For the ABoxUsb data path use EnableManaged below instead. In either mode
 * the owner implements USB SetFeature, timing, external VBUS and class logic.
 * Event storage remains valid until disable/stop. Callbacks run in IRQ/Poll
 * context and must not reenter. Serialize calls and other register access. */
ABoxHalStatus ABoxUsbOtg_Enable(const ABoxUsbOtgEvents *events);
ABoxHalStatus ABoxUsbOtg_Disable(void);
ABoxHalStatus ABoxUsbOtg_GetStatus(ABoxUsbOtgStatus *status);
/* Set HNP permission after the upper USB stack completes SetFeature. The
 * controller selects the host/device enable bit from its current role. */
ABoxHalStatus ABoxUsbOtg_SetHnpPermission(uint8_t enabled);
/* B-device, device role, after HNP permission and USB suspend. A return of OK
 * only means the request bit was set; completion is asynchronous. */
ABoxHalStatus ABoxUsbOtg_RequestHnp(void);
/* B-device, device role, no valid B-session. The owner observes the required
 * VBUS discharge and bus timing before requesting SRP. */
ABoxHalStatus ABoxUsbOtg_RequestSrp(void);
/* Call from the OTG IRQ before any vendor handler consumes OTG flags. */
void ABoxUsbOtg_Irq(void);
/* Attach automatic role adoption to an already started ABoxUsb controller.
 * alternate describes the other role with its own FIFO allocation. IRQ must
 * call ABoxUsb_Irq, and the owner must call ABoxUsb_Poll promptly in task
 * context; Poll latency must satisfy the USB OTG role-change timing. The latter
 * completes the switch and emits ABOX_USB_ROLE_CHANGED
 * (number = ABoxUsbRole), or ABOX_USB_ROLE_ERROR (number = ABoxHalStatus).
 * Old transfers complete CANCELLED after the hardware is quiesced; all old
 * endpoint/pipe handles are invalid. A failed adoption shuts down the core;
 * Close and reopen it before reuse. Both role configs require VBUS sensing.
 * Upper USB protocol and external VBUS remain the
 * owner's responsibility. Stop detaches managed OTG automatically. Do not
 * call the standalone Enable/Disable/Irq while managed mode is active. */
ABoxHalStatus ABoxUsbOtg_EnableManaged(const ABoxUsbConfig *alternate,const ABoxUsbOtgEvents *events);
/* Host suspend starts HNP after the upper layer sends SetFeature. To resume,
 * assert resume, wait the USB-specified interval using the system timer, then
 * deassert it. No active host transfers may be outstanding. */
ABoxHalStatus ABoxUsbOtg_HostSuspend(void);
ABoxHalStatus ABoxUsbOtg_HostResume(uint8_t asserted);
#endif
