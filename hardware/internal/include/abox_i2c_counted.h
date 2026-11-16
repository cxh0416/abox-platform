#ifndef ABOX_I2C_COUNTED_H
#define ABOX_I2C_COUNTED_H
#include "abox_i2c.h"
/* SMBus counted read: write the command/payload, then receive the count and
 * 1..32 data bytes plus an optional trailing PEC. `read` has space for
 * capacity+trailing bytes. Output is valid on success. */
ABoxHalStatus ABoxI2c_TransferCounted(unsigned instance,uint8_t address,
                                     const uint8_t *write,uint8_t write_length,
                                     uint8_t *read,uint8_t capacity,uint8_t trailing,
                                     uint8_t *count,uint32_t timeout_ms);
#endif
