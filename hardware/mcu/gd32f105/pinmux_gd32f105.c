#include "abox_pinmux_backend.h"
#include "gd32f10x.h"
void ABoxPinMuxHw_Enable(void) { rcu_periph_clock_enable(RCU_AF); }
uint32_t ABoxPinMuxHw_Read(void) { return AFIO_PCF0; }
void ABoxPinMuxHw_Write(uint32_t value) { AFIO_PCF0 = value; }
