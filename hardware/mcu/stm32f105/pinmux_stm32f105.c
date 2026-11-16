#include "abox_pinmux_backend.h"
#include "stm32f1xx_hal.h"
void ABoxPinMuxHw_Enable(void) { __HAL_RCC_AFIO_CLK_ENABLE(); }
uint32_t ABoxPinMuxHw_Read(void) { return AFIO->MAPR; }
void ABoxPinMuxHw_Write(uint32_t value) { AFIO->MAPR = value; }
