#ifndef ABOX_LOW_POWER_BACKEND_H
#define ABOX_LOW_POWER_BACKEND_H
#include "abox_low_power.h"
int ABoxLowPowerHw_CanEnter(void);
int ABoxLowPowerHw_OnWakeClock(void);
int ABoxLowPowerHw_SourceMatches(ABoxMcuClockSource source);
void ABoxLowPowerHw_Enter(ABoxLowPowerMode mode,ABoxLowPowerWait wait,uint8_t low_regulator);
uint8_t ABoxLowPowerHw_Flags(void);
void ABoxLowPowerHw_ClearFlags(uint8_t flags);
void ABoxLowPowerHw_SetWakePin(uint8_t enabled);
#endif
