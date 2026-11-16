#ifndef ABOX_I2C_F105_REGS_H
#define ABOX_I2C_F105_REGS_H
#include <stdint.h>

/* Register offsets and common bit positions of the STM32F105/GD32F105 I2C. */
typedef struct {
    volatile uint32_t control1, control2, own1, own2, data, status1, status2, clock, rise;
} ABoxI2cF105Registers;
#define ABOX_I2C_CR1_PE        (1U << 0)
#define ABOX_I2C_CR1_SMBUS     (1U << 1)
#define ABOX_I2C_CR1_SMBTYPE   (1U << 3)
#define ABOX_I2C_CR1_GC        (1U << 6)
#define ABOX_I2C_CR1_NOSTRETCH (1U << 7)
#define ABOX_I2C_CR1_START     (1U << 8)
#define ABOX_I2C_CR1_ACK       (1U << 10)
#define ABOX_I2C_CR2_IRQ       ((1U << 8) | (1U << 9) | (1U << 10))
#define ABOX_I2C_SR1_SB        (1U << 0)
#define ABOX_I2C_SR1_ADDR      (1U << 1)
#define ABOX_I2C_SR1_BTF       (1U << 2)
#define ABOX_I2C_SR1_STOP      (1U << 4)
#define ABOX_I2C_SR1_RXNE      (1U << 6)
#define ABOX_I2C_SR1_TXE       (1U << 7)
#define ABOX_I2C_SR1_BERR      (1U << 8)
#define ABOX_I2C_SR1_ARLO      (1U << 9)
#define ABOX_I2C_SR1_AF        (1U << 10)
#define ABOX_I2C_SR1_OVR       (1U << 11)
#define ABOX_I2C_SR1_TIMEOUT   (1U << 14)
#define ABOX_I2C_SR1_SMBALERT  (1U << 15)
#define ABOX_I2C_SR2_TRA       (1U << 2)
#define ABOX_I2C_SR2_GC        (1U << 4)
#define ABOX_I2C_SR2_DUAL      (1U << 7)
#endif
