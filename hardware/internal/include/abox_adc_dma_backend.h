#ifndef ABOX_ADC_DMA_BACKEND_H
#define ABOX_ADC_DMA_BACKEND_H
#include "abox_adc.h"
#include <stdint.h>
ABoxHalStatus ABoxAdcHw_DmaReserve(unsigned id);
ABoxHalStatus ABoxAdcHw_DmaPrepare(unsigned id,const ABoxAdcDmaConfig *config);
uintptr_t ABoxAdcHw_DmaAddress(unsigned id);
ABoxHalStatus ABoxAdcHw_DmaStart(unsigned id,const ABoxAdcDmaConfig *config);
void ABoxAdcHw_DmaPause(unsigned id);
ABoxHalStatus ABoxAdcHw_DmaTriggerInjected(unsigned id);
uint8_t ABoxAdcHw_DmaInjectedFetch(unsigned id,uint8_t count,uint16_t *samples);
void ABoxAdcHw_DmaInjectedMask(unsigned id);
void ABoxAdcDma_InjectedIrq(unsigned id);
ABoxHalStatus ABoxAdcHw_DmaRelease(unsigned id,int fault);
ABoxHalStatus ABoxAdcHw_DualReserve(void);
ABoxHalStatus ABoxAdcHw_DualPrepare(const ABoxAdcDualDmaConfig *config);
ABoxHalStatus ABoxAdcHw_DualStart(const ABoxAdcDualDmaConfig *config);
void ABoxAdcHw_DualRegularPause(void);
void ABoxAdcHw_DualPause(void);
ABoxHalStatus ABoxAdcHw_DualRelease(int fault);
#endif
