#ifndef ABOX_CAPTURE_DMA_BACKEND_H
#define ABOX_CAPTURE_DMA_BACKEND_H
#include "abox_timer.h"
#include <stdint.h>
ABoxHalStatus ABoxCaptureHw_DmaReserve(unsigned id,unsigned channel);
uintptr_t ABoxCaptureHw_DmaAddress(unsigned id,unsigned channel);
void ABoxCaptureHw_DmaEnable(unsigned id,unsigned channel);
void ABoxCaptureHw_DmaPause(unsigned id,unsigned channel);
int ABoxCaptureHw_DmaOvercapture(unsigned id,unsigned channel);
void ABoxCaptureHw_DmaRelease(unsigned id,unsigned channel);
#endif
