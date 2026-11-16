#ifndef ABOX_ADC_WATCH_BACKEND_H
#define ABOX_ADC_WATCH_BACKEND_H
#include "abox_adc.h"
ABoxHalStatus ABoxAdcHw_WatchConfigure(unsigned id,const ABoxAdcWatchConfig *config);
uint8_t ABoxAdcHw_WatchPending(unsigned id);
void ABoxAdcHw_WatchClear(unsigned id);
void ABoxAdcHw_WatchDisable(unsigned id);
uint8_t ABoxAdcWatch_Configured(unsigned id);
#endif
