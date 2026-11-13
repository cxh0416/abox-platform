#ifndef ABOX_MCU_H
#define ABOX_MCU_H
#include <stdint.h>
/* The 96-bit factory UID is returned as three ordered 32-bit words. */
uint32_t ABoxMcu_UidWord(unsigned index);
#endif
