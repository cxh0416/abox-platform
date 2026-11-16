#ifndef ABOX_I2C_H
#define ABOX_I2C_H
#include <stdint.h>
#include "abox_hal_status.h"

/* Physical I2C1/I2C2. GPIO open-drain mode, remapping and external pullups
 * are configured separately by the board. */
typedef enum { ABOX_I2C_DUTY_2, ABOX_I2C_DUTY_16_9 } ABoxI2cDuty;
typedef struct { uint32_t maximum_hz; ABoxI2cDuty fast_duty; } ABoxI2cConfig;
typedef enum { ABOX_I2C_ADDRESS_7BIT, ABOX_I2C_ADDRESS_10BIT } ABoxI2cAddressMode;
typedef struct { uint16_t value; ABoxI2cAddressMode mode; } ABoxI2cAddress;

/* Open selects a representable clock that does not exceed maximum_hz. Fractional
 * MHz APB1 clocks are supported; the F105 frequency field records the integer
 * MHz part, while SCL and rise-time calculations use the actual APB1 Hz. The
 * caller reserves and serializes the controller and provides a bound,
 * advancing platform tick. Blocking transfers are not safe in an interrupt
 * or critical section that prevents that tick from advancing. */
ABoxHalStatus ABoxI2c_Open(unsigned instance, const ABoxI2cConfig *config);
ABoxHalStatus ABoxI2c_Close(unsigned instance);
/* Select F105 SMBus host mode while idle. Hardware then detects its SMBus
 * clock-low timeout and reports BUS_TIMEOUT; timing and external recovery still
 * require board validation. Mode resets on Close/Open. */
ABoxHalStatus ABoxI2c_SetSmbusMode(unsigned instance,uint8_t enabled);
/* Read the SMBALERT event flag, optionally acknowledging it. The board owns
 * the SMBA pin and external line; the flag can coalesce multiple alerts. Poll
 * only when the slave listener is stopped, since its IRQ owns that flag. */
ABoxHalStatus ABoxI2c_SmbusAlert(unsigned instance,uint8_t clear,uint8_t *pending);
/* Unshifted 7- or 10-bit address. write and read may be used separately or together;
 * combined transactions use a repeated START. Buffers must not overlap and
 * read data is valid only on success. Timeout applies to the entire transfer.
 * Without a listener, NACK, arbitration loss, error or mid-transfer timeout
 * requires Close/Open. With a listener, NACK after a completed STOP and
 * arbitration loss return to listening; other master errors fault the bus and
 * require Stop/Abort and Close/Open. The board owns external bus recovery. */
ABoxHalStatus ABoxI2c_Transfer(unsigned instance, ABoxI2cAddress address,
                               const uint8_t *write, uint16_t write_length,
                               uint8_t *read, uint16_t read_length,
                               uint32_t timeout_ms);
/* Address-only transaction for SMBus Quick Command. Direction is the bit sent
 * with the address; no data byte follows. Same ownership, timeout and recovery
 * rules as Transfer. Some devices use this command to change state. */
ABoxHalStatus ABoxI2c_Quick(unsigned instance,uint8_t address,uint8_t read_direction,uint32_t timeout_ms);
/* Same master transaction and total timeout as Transfer, with fixed DMA
 * channels for bulk TX/RX. A single-byte receive uses the polling sequence
 * required by the F105 I2C peripheral. The caller must enable and dispatch DMA
 * channel interrupts and provide an advancing tick; do not call from an IRQ
 * that masks either. DMA RX completion generates STOP in its IRQ; success is
 * returned only after STOP completes. Buffers remain owned until return.
 * Error recovery follows Transfer; a partial bus transaction may already have
 * occurred. */
ABoxHalStatus ABoxI2c_TransferDma(unsigned instance,ABoxI2cAddress address,
                                  const uint8_t *write,uint16_t write_length,
                                  uint8_t *read,uint16_t read_length,uint32_t timeout_ms);

typedef struct {
    ABoxI2cAddress own_address;
    uint8_t dual_address;       /* 7-bit own address only */
    uint8_t second_address;     /* unshifted 7-bit address */
    uint8_t general_call;
    uint8_t clock_stretch;
} ABoxI2cSlaveConfig;
typedef enum { ABOX_I2C_SLAVE_PRIMARY, ABOX_I2C_SLAVE_SECONDARY,
               ABOX_I2C_SLAVE_GENERAL_CALL } ABoxI2cSlaveMatch;
typedef enum { ABOX_I2C_SLAVE_WRITE, ABOX_I2C_SLAVE_READ } ABoxI2cSlaveDirection;
typedef enum { ABOX_I2C_SLAVE_STOP, ABOX_I2C_SLAVE_RESTART,
               ABOX_I2C_SLAVE_NACK } ABoxI2cSlaveEnd;
typedef struct {
    void *context;
    void (*addressed)(void *context, unsigned instance, ABoxI2cSlaveDirection direction,
                      ABoxI2cSlaveMatch match);
    void (*received)(void *context, unsigned instance, uint8_t byte);
    uint8_t (*transmit)(void *context, unsigned instance);
    void (*ended)(void *context, unsigned instance, ABoxI2cSlaveEnd reason);
    void (*error)(void *context, unsigned instance, ABoxHalStatus status);
    void (*smbus_alert)(void *context,unsigned instance);
} ABoxI2cSlaveEvents;
/* Listen can coexist with blocking master calls on the opened controller. A
 * requested master START waits for a preceding slave transfer to finish;
 * timeout before START returns BUSY and leaves the listener running. During
 * a master transfer slave IRQs are deferred and resume on success, completed
 * NACK or arbitration loss. The F105 cannot acknowledge its own address in
 * the same transfer in which it lost arbitration; a later repeated START can
 * address it. The application must handle retries as needed. The board configures pins,
 * NVIC and dispatches both event and error vectors to ABoxI2c_Irq. Callbacks
 * run in interrupt context and must not reenter the controller; null receive
 * callbacks discard data and null transmit callbacks send 0xff. A read ends
 * with the master's NACK; a subsequent STOP does not emit a second end event.
 * Disabling clock stretching requires the callbacks to meet the bus timing.
 * The event table and context remain valid until Stop/Abort. Stop returns BUSY
 * while addressed or when an address match is pending. Coordinate Stop with
 * the external master so that no new transaction starts during shutdown.
 * Initialize unused event fields to zero; the IRQ acknowledges SMBALERT and
 * calls smbus_alert when configured in SMBus mode.
 * Abort may interrupt the bus and requires Close/Open. A bus
 * error disables listening; Stop and Close/Open before trying again. Calls
 * on one controller, except its IRQ dispatch, must be serialized. */
ABoxHalStatus ABoxI2c_SlaveStart(unsigned instance, const ABoxI2cSlaveConfig *config,
                                  const ABoxI2cSlaveEvents *events);
ABoxHalStatus ABoxI2c_SlaveStop(unsigned instance);
ABoxHalStatus ABoxI2c_SlaveAbort(unsigned instance);
void ABoxI2c_Irq(unsigned instance);
#endif
