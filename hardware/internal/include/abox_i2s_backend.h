#ifndef ABOX_I2S_BACKEND_H
#define ABOX_I2S_BACKEND_H
#include "abox_hal_status.h"
#include "abox_i2s.h"
#include <stdint.h>
typedef struct {
    volatile uint32_t control1,control2,status,data,crc_poly,rx_crc,tx_crc,i2s_config,i2s_prescaler;
} ABoxI2sRegisters;
ABoxHalStatus ABoxI2sHw_Reserve(unsigned instance);
ABoxI2sRegisters *ABoxI2sHw_Registers(unsigned instance);
void ABoxI2sHw_Release(unsigned instance);
typedef struct { volatile uint32_t control,reserved[10],config; } ABoxI2sClockRegisters;
ABoxI2sClockRegisters *ABoxI2sHw_ClockRegisters(void);
uint32_t ABoxI2sHw_ExternalClockHz(void);
int ABoxI2sHw_FormatSupported(const ABoxI2sConfig *config);
#endif
