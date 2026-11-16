#ifndef ABOX_I2C_BACKEND_H
#define ABOX_I2C_BACKEND_H
#include <stdint.h>
#include "abox_i2c.h"
typedef enum { ABOX_I2C_HW_BUSY, ABOX_I2C_HW_START, ABOX_I2C_HW_ADDRESS,
               ABOX_I2C_HW_TX_EMPTY, ABOX_I2C_HW_FINISHED, ABOX_I2C_HW_RX_READY,
               ABOX_I2C_HW_STOP_PENDING, ABOX_I2C_HW_HEADER10 } ABoxI2cHwFlag;
uint32_t ABoxI2cHw_Pclk(void);
uint32_t ABoxI2cHw_MaxPclk(void);
ABoxHalStatus ABoxI2cHw_Open(unsigned id, uint32_t speed, ABoxI2cDuty duty);
ABoxHalStatus ABoxI2cHw_Close(unsigned id);
int ABoxI2cHw_Flag(unsigned id, ABoxI2cHwFlag flag);
ABoxHalStatus ABoxI2cHw_Error(unsigned id);
void ABoxI2cHw_Start(unsigned id);
void ABoxI2cHw_Stop(unsigned id);
void ABoxI2cHw_Address(unsigned id, uint8_t address, int read);
void ABoxI2cHw_AddressByte(unsigned id, uint8_t value);
uintptr_t ABoxI2cHw_Base(unsigned id);
uint32_t ABoxI2cHw_OwnAddress(ABoxI2cAddress address);
void ABoxI2cHw_ClearAddress(unsigned id);
void ABoxI2cHw_Ack(unsigned id, int enabled);
void ABoxI2cHw_AckNext(unsigned id, int next);
void ABoxI2cHw_Write(unsigned id, uint8_t value);
uint8_t ABoxI2cHw_Read(unsigned id);
uintptr_t ABoxI2cHw_DmaAddress(unsigned id);
void ABoxI2cHw_DmaRequest(unsigned id,int enabled,int last);
uint32_t ABoxI2cHw_EnterCritical(void);
void ABoxI2cHw_LeaveCritical(uint32_t key);
#endif
