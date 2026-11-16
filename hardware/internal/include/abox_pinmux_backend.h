#ifndef ABOX_PINMUX_BACKEND_H
#define ABOX_PINMUX_BACKEND_H
#include <stdint.h>
void ABoxPinMuxHw_Enable(void);
uint32_t ABoxPinMuxHw_Read(void);
void ABoxPinMuxHw_Write(uint32_t value);
#endif
