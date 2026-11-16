#ifndef ABOX_USB_H
#define ABOX_USB_H
#include <stdint.h>
#include "abox_hal_status.h"

/* One physical OTG FS controller, used exclusively as a host or a device. */
typedef enum { ABOX_USB_DEVICE, ABOX_USB_HOST } ABoxUsbRole;
typedef enum { ABOX_USB_CONTROL, ABOX_USB_ISOCHRONOUS, ABOX_USB_BULK, ABOX_USB_INTERRUPT } ABoxUsbEndpointType;
typedef enum { ABOX_USB_FULL_SPEED, ABOX_USB_LOW_SPEED } ABoxUsbSpeed;
typedef enum { ABOX_USB_SETUP, ABOX_USB_DATA0, ABOX_USB_DATA1 } ABoxUsbPid;
typedef enum { ABOX_USB_TRANSFER_OK, ABOX_USB_TRANSFER_NAK, ABOX_USB_TRANSFER_STALL,
               ABOX_USB_TRANSFER_ERROR, ABOX_USB_TRANSFER_CANCELLED } ABoxUsbTransferResult;
typedef enum { ABOX_USB_RESET, ABOX_USB_SUSPEND, ABOX_USB_RESUME, ABOX_USB_CONNECTED,
               ABOX_USB_DISCONNECTED, ABOX_USB_PORT_ENABLED, ABOX_USB_PORT_DISABLED,
               ABOX_USB_SOF, ABOX_USB_ISO_INCOMPLETE,
               ABOX_USB_ROLE_CHANGED, ABOX_USB_ROLE_ERROR } ABoxUsbEvent;
/* An incomplete isochronous event normally names the device endpoint.
 * Some controllers only report the direction; these values mean that the
 * owner must inspect the active isochronous endpoints in that direction. */
#define ABOX_USB_ANY_IN_ENDPOINT 0xffU
#define ABOX_USB_ANY_OUT_ENDPOINT 0x7fU
typedef struct {
    void *context;
    void (*event)(void *context,ABoxUsbEvent event,unsigned number);
    /* setup contains exactly eight wire-order bytes and is valid only during
 * the callback. The upper USB stack owns requests, descriptors and class
 * protocols; it must apply a new device address after the status stage. A new
 * SETUP supersedes the previous control request: copy the packet, cancel any
 * still-active EP0 stages, and process it after their completions. */
    void (*setup)(void *context,const uint8_t setup[8]);
    /* Device: number is the endpoint address including bit 7 for IN. Host:
     * number is the hardware channel. count is bytes actually transferred. */
    void (*complete)(void *context,unsigned number,ABoxUsbTransferResult result,uint32_t count);
} ABoxUsbEvents;
typedef struct {
    ABoxUsbRole role;
    uint32_t usb_clock_hz; /* actual board-configured USB clock: 48 MHz */
    uint16_t rx_fifo_words;
    /* Device: TX FIFO 0..3. Host: nonperiodic and periodic TX FIFOs in 0,1;
     * entries 2,3 must be zero. Total RX + TX is at most 320 32-bit words. */
    uint16_t tx_fifo_words[4];
    uint8_t vbus_sensing,sof_events;
} ABoxUsbConfig;
typedef struct {
    uint8_t address; /* bit 7 IN, low nibble 0..3 */
    ABoxUsbEndpointType type;
    uint16_t max_packet;
} ABoxUsbDeviceEndpoint;
typedef struct {
    uint8_t address; /* bit 7 IN, endpoint 0..15 */
    uint8_t device_address; /* 0..127 */
    ABoxUsbSpeed speed;
    ABoxUsbEndpointType type;
    uint16_t max_packet;
} ABoxUsbHostPipe;

/* Board code owns GPIO, alternate functions, clock-tree selection, IRQ/NVIC,
 * connector ID and the external VBUS power switch/overcurrent circuit. The
 * caller supplies and verifies the real USB clock and serializes operations;
 * it must not run another USB stack or touch this controller concurrently.
 * Callbacks run in USB IRQ or Poll context and must not call back into this
 * USB API; enqueue work for the owner. Callback storage remains valid until Close.
 * These are controller primitives, not a USB device/host class stack. */
ABoxHalStatus ABoxUsb_Open(const ABoxUsbConfig *config,const ABoxUsbEvents *events);
ABoxHalStatus ABoxUsb_Start(void);
/* Stop requires no outstanding transfers and forgets endpoint/pipe state.
 * Start after a successful Stop reinitializes the same controller config.
 * If Stop fails the controller is stopped and must be closed before reuse. */
ABoxHalStatus ABoxUsb_Stop(void);
ABoxHalStatus ABoxUsb_Close(void);
void ABoxUsb_Irq(void);
/* Call from the serialized USB owner while processing host events. It also
 * retires halted channels on F105 revisions that omit the halt interrupt. */
void ABoxUsb_Poll(void);

/* Device mode: reset cancels active transfers and invalidates all endpoints. Open both endpoint-zero
 * directions after reset. Endpoint-zero transfers are limited to one packet;
 * the upper stack sequences longer control stages. Transfer buffers remain valid through the
 * completion callback or cancellation. IN sends, OUT receives. OUT capacity
 * must cover a whole number of max-packet bytes; all capacities are rounded
 * up to a 32-bit word for FIFO access. A zero-length
 * status stage is allowed. Host/device connection does not imply enumeration. */
ABoxHalStatus ABoxUsbDevice_OpenEndpoint(const ABoxUsbDeviceEndpoint *endpoint);
ABoxHalStatus ABoxUsbDevice_CloseEndpoint(uint8_t address);
ABoxHalStatus ABoxUsbDevice_Transfer(uint8_t address,uint8_t *buffer,uint16_t length,uint16_t capacity);
/* Cancellation is asynchronous; Poll completes it if no interrupt arrives.
 * The buffer remains owned until the CANCELLED/ERROR completion callback. */
ABoxHalStatus ABoxUsbDevice_Cancel(uint8_t address);
ABoxHalStatus ABoxUsbDevice_SetStall(uint8_t address,uint8_t stall);
ABoxHalStatus ABoxUsbDevice_SetAddress(uint8_t address);
/* Assert/deassert device remote-wakeup signalling. The upper stack must first
 * obtain the host's permission and observe the required suspend interval;
 * it must time and end the signalling according to USB. Assertion requires
 * the controller to report suspend; OK means the request was written, not
 * that the host resumed. Deassert is permitted after host wakeup. */
ABoxHalStatus ABoxUsbDevice_RemoteWakeup(uint8_t asserted);

/* Host mode: the board enables external VBUS before Start. ResetPort uses an
 * advancing millisecond tick and blocks for the USB reset interval. Each pipe
 * reserves a channel 0..7. The upper stack owns enumeration, retries, control
 * stages, polling intervals and data PID selection. Each Submit describes at
 * most one packet; upper layers split longer transfers. IN buffers must be sized
 * to a whole number of max-packet bytes; pass capacity separately from length.
 * All buffers need capacity rounded up to a 32-bit word for FIFO access.
 * Hardware FIFO limits can reject large packets even when their USB type permits
 * them. At least one host TX FIFO must accommodate an IN endpoint's packet to
 * avoid the F105 host receive FIFO erratum.
 * Do not close/reuse a channel while a transfer is outstanding. */
ABoxHalStatus ABoxUsbHost_ResetPort(void);
ABoxHalStatus ABoxUsbHost_OpenPipe(unsigned channel,const ABoxUsbHostPipe *pipe);
ABoxHalStatus ABoxUsbHost_ClosePipe(unsigned channel);
ABoxHalStatus ABoxUsbHost_Submit(unsigned channel,ABoxUsbPid pid,uint8_t *buffer,
                                 uint16_t length,uint16_t capacity);
/* Halt is asynchronous. Keep the buffer until its completion, including
 * after disconnect; call Poll if the controller omits a halt interrupt. */
ABoxHalStatus ABoxUsbHost_Halt(unsigned channel);
ABoxHalStatus ABoxUsbHost_PortSpeed(ABoxUsbSpeed *speed);
ABoxHalStatus ABoxUsbHost_Frame(uint16_t *frame);
#endif
