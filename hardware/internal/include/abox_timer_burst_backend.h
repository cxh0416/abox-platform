#ifndef ABOX_TIMER_BURST_BACKEND_H
#define ABOX_TIMER_BURST_BACKEND_H
#include "abox_hal_status.h"
#include <stdint.h>
ABoxHalStatus ABoxTimerBurstHw_Reserve(unsigned instance);
void ABoxTimerBurstHw_Config(unsigned instance,uint8_t base,uint8_t length);
void ABoxTimerBurstHw_Request(unsigned instance,unsigned request,int enabled);
uintptr_t ABoxTimerBurstHw_Address(unsigned instance);
void ABoxTimerBurstHw_Release(unsigned instance);
#endif
