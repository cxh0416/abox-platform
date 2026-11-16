#ifndef ABOX_DAC_BACKEND_H
#define ABOX_DAC_BACKEND_H
#include "abox_dac.h"
ABoxHalStatus ABoxDacHw_Open(unsigned channel, const ABoxDacConfig *config);
void ABoxDacHw_Enable(unsigned channel);
void ABoxDacHw_Disable(unsigned channel);
void ABoxDacHw_Write(unsigned channel, uint16_t value);
void ABoxDacHw_Trigger(unsigned channel);
uint16_t ABoxDacHw_ReadOutput(unsigned channel);
uintptr_t ABoxDacHw_DmaAddress(unsigned channel);
void ABoxDacHw_DmaRequest(unsigned channel, int enable);
#endif
