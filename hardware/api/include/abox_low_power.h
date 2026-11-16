#ifndef ABOX_LOW_POWER_H
#define ABOX_LOW_POWER_H
#include <stdint.h>
#include "abox_hal_status.h"
#include "abox_mcu_clock.h"

typedef enum { ABOX_LOW_POWER_SLEEP, ABOX_LOW_POWER_STOP, ABOX_LOW_POWER_STANDBY } ABoxLowPowerMode;
typedef enum { ABOX_LOW_POWER_WFI, ABOX_LOW_POWER_WFE } ABoxLowPowerWait;
typedef struct {
    ABoxLowPowerMode mode;
    ABoxLowPowerWait wait;
    uint8_t low_power_regulator; /* STOP only */
    void *context;
    ABoxHalStatus (*restore)(void *context); /* required for STOP */
} ABoxLowPowerConfig;
#define ABOX_LOW_POWER_WAKE_FLAG 1U
#define ABOX_LOW_POWER_STANDBY_FLAG 2U

/* The board owns wake sources, their NVIC/EXTI setup, peripheral quiescing,
 * oscillator dependencies, watchdog and elapsed-time accounting. Call only
 * from thread context with interrupts unmasked; coordinate with the scheduler.
 * Suspend the periodic tick if it should not wake the CPU. WFE clears the old
 * event before waiting; the caller must prevent a wake event being lost while
 * preparing. SLEEP leaves the clock tree unchanged. STOP wakes on HSI/IRC8M;
 * restore is called after wake even if entry was aborted. It must restore any
 * tick needed for timeouts, restore the clock and reconfigure affected timebases
 * and peripherals. Interrupts may run before restore: their handlers must be
 * safe at the wake clock. On restore failure the system remains in its actual
 * clock state and the error is returned; do not resume dependent peripherals.
 * STANDBY normally exits by reset and does not return. A return is ERROR; wake
 * flags are never cleared implicitly. Check or clear them explicitly before
 * entry and after boot. This interface cannot prove actual power consumption. */
ABoxHalStatus ABoxLowPower_Enter(const ABoxLowPowerConfig *config);
/* Helper for a STOP restore callback. Tick/HAL timeouts must already advance;
 * it does not restore SysTick, board timing or peripheral configurations. The
 * helper restores a non-HSI source even when it has the same frequency as HSI. */
ABoxHalStatus ABoxLowPower_RestoreClock(const ABoxMcuClockConfig *config);
ABoxHalStatus ABoxLowPower_GetFlags(uint8_t *flags);
ABoxHalStatus ABoxLowPower_ClearFlags(uint8_t flags);
/* Physical WKUP pin, board-owned and independent of RTC/EXTI wake sources. */
ABoxHalStatus ABoxLowPower_SetWakePin(uint8_t enabled);
#endif
