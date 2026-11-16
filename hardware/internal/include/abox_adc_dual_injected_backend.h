#ifndef ABOX_ADC_DUAL_INJECTED_BACKEND_H
#define ABOX_ADC_DUAL_INJECTED_BACKEND_H
#include "abox_adc.h"
ABoxHalStatus ABoxAdcHw_DualInjectedStart(const ABoxAdcDualInjectedConfig *config);
uint8_t ABoxAdcHw_DualInjectedReady(void);
uint8_t ABoxAdcHw_DualInjectedFirstReady(void);
void ABoxAdcHw_DualInjectedRead(ABoxAdcDualSample *samples,uint8_t count);
void ABoxAdcHw_DualInjectedStop(void);
uint8_t ABoxAdcHw_DualInjectedFetch(uint8_t count,ABoxAdcDualSample *samples);
#endif
