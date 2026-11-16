#ifndef ABOX_SMBUS_H
#define ABOX_SMBUS_H
#include <stdint.h>
#include "abox_hal_status.h"
/* Fixed-length SMBus/PMBus master transactions over an already-open I2C bus.
 * Address is unshifted 7-bit; write bytes may include a command/count and read
 * bytes exclude PEC. Write-only, read-only and repeated-start write/read are
 * supported. With PEC, CRC-8 polynomial 0x07 covers address bytes, direction
 * bits and all wire bytes. A failed PEC returns ERROR without modifying read.
 * A bus error follows the I2C Close/Open recovery contract; PEC mismatch alone
 * does not fault the controller. General slave protocol requires additional support.
 * Whole-transfer timeout and bus timings are caller-selected; a successful
 * transaction does not establish compliance with SMBus electrical/timing rules. */
ABoxHalStatus ABoxSmbus_Transfer(unsigned instance,uint8_t address,
                                 const uint8_t *write,uint8_t write_length,
                                 uint8_t *read,uint8_t read_length,uint8_t pec,
                                 uint32_t timeout_ms);
/* Block Read and Block Write-Block Read Process Call consume the device's
 * count byte (1..32), then that many data bytes and optional PEC. `read` has
 * capacity bytes; data and count are updated only on success. The F105 receiver
 * must service the count before the next byte completes; an already-detected
 * late service fails and requires I2C Close/Open. Keep interrupt latency short
 * relative to one bus byte. Buffer overflow, malformed count or bus failure
 * faults the controller. Actual bus timing still requires device validation. */
ABoxHalStatus ABoxSmbus_BlockRead(unsigned instance,uint8_t address,uint8_t command,
                                 uint8_t *read,uint8_t capacity,uint8_t *count,uint8_t pec,uint32_t timeout_ms);
ABoxHalStatus ABoxSmbus_BlockProcessCall(unsigned instance,uint8_t address,uint8_t command,
                                        const uint8_t *write,uint8_t write_length,
                                        uint8_t *read,uint8_t capacity,uint8_t *count,uint8_t pec,uint32_t timeout_ms);
/* Quick Command sends only the address and direction bit and has no PEC. */
ABoxHalStatus ABoxSmbus_Quick(unsigned instance,uint8_t address,uint8_t read_direction,uint32_t timeout_ms);
/* Optional F105 SMBus host mode enables the controller's line-low timeout.
 * Call while I2C is open and idle; hardware timing and board recovery require
 * verification. Close/Open resets this setting. */
ABoxHalStatus ABoxSmbus_SetHardwareMode(unsigned instance,uint8_t enabled);
ABoxHalStatus ABoxSmbus_AlertPending(unsigned instance,uint8_t clear,uint8_t *pending);
/* Issue an Alert Response Address read (0x0c). response is the complete wire
 * byte, whose upper seven bits identify the responding device; its low bit is
 * unspecified. The board monitors the SMBALERT line and decides whether to
 * repeat the read for other responders. NO_ACK means no device responded. */
ABoxHalStatus ABoxSmbus_ReadAlertResponse(unsigned instance,uint8_t pec,uint8_t *response,uint32_t timeout_ms);
typedef struct {
    void *context;
    void (*notify)(void *context,unsigned instance,uint8_t device_address,uint16_t status_word);
    void (*error)(void *context,unsigned instance,ABoxHalStatus status);
    void (*alert)(void *context,unsigned instance);
} ABoxSmbusHostNotifyEvents;
/* Listen at the SMBus host address 0x08 using the underlying I2C slave. A
 * complete Host Notify contains a source address byte and two data bytes,
 * without PEC. Callbacks run in IRQ context, are non-reentrant, and the event
 * table remains valid until Stop succeeds. A malformed or interrupted message
 * reports ERROR. Master commands may be issued while listening; before START
 * the listener services an already-addressed slave transaction. A master
 * arbitration loss is returned to the caller and hardware can acknowledge
 * its address after a subsequent repeated START, subject to F105 behavior.
 * Board code owns pins, NVIC and dispatch
 * to ABoxI2c_Irq; coordinate Stop with the other bus controllers. Enable F105
 * SMBus hardware mode before listening if the alert callback is required. */
ABoxHalStatus ABoxSmbus_HostNotifyStart(unsigned instance,const ABoxSmbusHostNotifyEvents *events);
ABoxHalStatus ABoxSmbus_HostNotifyStop(unsigned instance);
/* Abort may interrupt a bus transfer and requires I2C Close/Open. */
ABoxHalStatus ABoxSmbus_HostNotifyAbort(unsigned instance);
#endif
