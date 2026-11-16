#ifndef ABOX_EXTI_BACKEND_H
#define ABOX_EXTI_BACKEND_H
#include "abox_exti.h"
void ABoxExtiHw_Open(ABoxGpioPin pin, ABoxExtiEdge edge,uint8_t routes);
void ABoxExtiHw_OpenInternal(unsigned line,ABoxExtiEdge edge,uint8_t routes);
void ABoxExtiHw_Close(unsigned line);
uint16_t ABoxExtiHw_Pending(uint16_t lines);
void ABoxExtiHw_Clear(unsigned line);
void ABoxExtiHw_Trigger(unsigned line);
uint32_t ABoxExtiHw_RawPending(uint32_t lines);
uint32_t ABoxExtiHw_EnabledPending(uint32_t lines);
#endif
