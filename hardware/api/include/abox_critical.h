#ifndef ABOX_CRITICAL_H
#define ABOX_CRITICAL_H
#include <stdint.h>
/* Mask configurable interrupts and return the prior CPU mask. Restore the
 * returned value in LIFO order, on the same core and in the same execution
 * context. These calls do not replace scheduler locks, and critical sections
 * must remain short and must not block on an interrupt-driven peripheral. */
uint32_t ABoxCritical_Enter(void);
void ABoxCritical_Exit(uint32_t previous_mask);
#endif
