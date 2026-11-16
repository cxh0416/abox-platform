#ifndef ABOX_MCU_CLOCK_BACKEND_H
#define ABOX_MCU_CLOCK_BACKEND_H
#include "abox_mcu_clock.h"
ABoxHalStatus ABoxMcuClockHw_Apply(const ABoxMcuClockConfig *config, const ABoxMcuClockFrequencies *expected);
ABoxHalStatus ABoxMcuClockHw_Get(ABoxMcuClockFrequencies *out);
#endif
