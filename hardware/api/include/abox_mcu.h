#ifndef ABOX_MCU_H
#define ABOX_MCU_H
#include <stdint.h>
/* The 96-bit factory UID is returned as three ordered 32-bit words. */
uint32_t ABoxMcu_UidWord(unsigned index);

/* Multiple flags may be set by hardware. Reading does not clear them. */
#define ABOX_RESET_PIN       (1UL << 0)
#define ABOX_RESET_POWER_ON  (1UL << 1)
#define ABOX_RESET_WATCHDOG  (1UL << 2)
#define ABOX_RESET_WINDOW_WATCHDOG (1UL << 3)
#define ABOX_RESET_SOFTWARE  (1UL << 4)
#define ABOX_RESET_LOW_POWER (1UL << 5)
uint32_t ABoxMcu_ResetCauses(void);
/* Clearing affects all reset causes; one startup owner should snapshot them. */
void ABoxMcu_ClearResetCauses(void);
#endif
