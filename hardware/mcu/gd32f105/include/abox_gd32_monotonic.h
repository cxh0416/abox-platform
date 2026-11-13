#ifndef ABOX_GD32_MONOTONIC_H
#define ABOX_GD32_MONOTONIC_H
#include <stdint.h>
int ABoxGdMonotonic_Init(void);
uint64_t ABoxGdMonotonic_GetMs(void);
void ABoxGdMonotonic_IrqHandler(void);
#endif
