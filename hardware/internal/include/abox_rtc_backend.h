#ifndef ABOX_RTC_BACKEND_H
#define ABOX_RTC_BACKEND_H
#include "abox_rtc.h"
uint32_t ABoxRtcHw_AccessBegin(void);
void ABoxRtcHw_AccessEnd(uint32_t key);
int ABoxRtcHw_AccessReady(void);
uint32_t ABoxRtcHw_Apb1Hz(void);
unsigned ABoxRtcHw_Source(void);
ABoxHalStatus ABoxRtcHw_SetLseMode(uint8_t bypass);
void ABoxRtcHw_StartOscillator(ABoxRtcSource source);
int ABoxRtcHw_OscillatorReady(ABoxRtcSource source);
void ABoxRtcHw_SelectSource(ABoxRtcSource source);
void ABoxRtcHw_EnableClock(void);
void ABoxRtcHw_BeginSync(void);
int ABoxRtcHw_SyncReady(void);
int ABoxRtcHw_WriteReady(void);
void ABoxRtcHw_BeginConfig(void);
void ABoxRtcHw_EndConfig(void);
uint32_t ABoxRtcHw_ReadPrescaler(void);
uint32_t ABoxRtcHw_ReadCounterHigh(void);
uint32_t ABoxRtcHw_ReadCounterLow(void);
void ABoxRtcHw_WritePrescaler(uint32_t value);
void ABoxRtcHw_WriteCounter(uint32_t value);
void ABoxRtcHw_WriteAlarm(uint32_t value);
uint8_t ABoxRtcHw_Flags(void);
void ABoxRtcHw_ClearFlags(uint8_t flags);
void ABoxRtcHw_SetInterrupts(uint8_t mask);
uint8_t ABoxRtcHw_ReadCalibration(void);
void ABoxRtcHw_WriteCalibration(uint8_t pulses);
uint32_t ABoxRtcHw_ReadOutputBits(void);
void ABoxRtcHw_WriteOutputBits(uint32_t bits);
void ABoxRtcHw_ResetBackup(void);
#endif
