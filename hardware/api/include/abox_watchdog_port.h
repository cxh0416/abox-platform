#ifndef ABOX_WATCHDOG_PORT_H
#define ABOX_WATCHDOG_PORT_H
#include <stdint.h>

/* Prescaler is 4, 8, 16, 32, 64, 128 or 256; reload is 0..4095.
 * Actual timeout depends on the MCU's low-speed oscillator tolerance. */
int ABoxWatchdog_Init(uint16_t prescaler, uint16_t reload);
int ABoxWatchdog_Refresh(void);
void ABoxWatchdog_FreezeWhileDebugging(void);
#endif
