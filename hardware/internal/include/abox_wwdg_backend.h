#ifndef ABOX_WWDG_BACKEND_H
#define ABOX_WWDG_BACKEND_H
#include <stdint.h>
typedef struct {
    volatile uint32_t control, configuration, status;
} ABoxWwdgRegisters;
ABoxWwdgRegisters *ABoxWwdgHw_Enable(void);
void ABoxWwdgHw_Start(ABoxWwdgRegisters *registers,uint8_t counter,uint32_t configuration);
void ABoxWwdgHw_SetDebugFreeze(uint8_t enabled);
#endif
