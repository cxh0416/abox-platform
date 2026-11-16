#ifndef ABOX_PWM_DMA_BACKEND_H
#define ABOX_PWM_DMA_BACKEND_H
#include <stdint.h>
#include "abox_hal_status.h"
ABoxHalStatus ABoxPwmHw_DmaReserve(unsigned instance, unsigned channel, uint16_t initial,
                                   const uint16_t *samples, uint16_t count);
ABoxHalStatus ABoxPwmHw_DmaPrepare(unsigned instance, unsigned channel, uint16_t initial);
uintptr_t ABoxPwmHw_DmaAddress(unsigned instance, unsigned channel);
void ABoxPwmHw_DmaEnable(unsigned instance);
void ABoxPwmHw_DmaPause(unsigned instance);
void ABoxPwmHw_DmaRelease(unsigned instance, unsigned channel);
#endif
