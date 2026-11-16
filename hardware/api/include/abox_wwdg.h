#ifndef ABOX_WWDG_H
#define ABOX_WWDG_H
#include <stdint.h>
#include "abox_hal_status.h"

typedef struct {
    uint8_t prescaler;       /* APB1 / 4096 / (1, 2, 4 or 8) */
    uint8_t window;          /* hardware window value, 0..127 */
    uint8_t initial_counter; /* 64..127 */
    uint8_t early_interrupt;
} ABoxWwdgConfig;
typedef struct {
    void *context;
    void (*on_early_warning)(void *context);
} ABoxWwdgEvents;

/* The window watchdog cannot be stopped without resetting the MCU. Start
 * reserves its registers and APB1 clock for the rest of this boot; it returns
 * BUSY if another owner already started it. The application supplies NVIC and
 * dispatches the early-warning IRQ to ABoxWwdg_Irq. Events and context must
 * remain valid until reset. The callback runs in IRQ context, very shortly
 * before reset; it must not block. A window <= 64 cannot be refreshed through
 * this API and can be used to deliberately allow expiry. */
ABoxHalStatus ABoxWwdg_Start(const ABoxWwdgConfig *config,const ABoxWwdgEvents *events);
/* Explicitly take over a watchdog already running (for example, from Boot)
 * without changing its counter, window, prescaler or pending flag. The prior
 * owner must stop accessing it and coordinate IRQ handoff before Attach. */
ABoxHalStatus ABoxWwdg_Attach(const ABoxWwdgEvents *events);
/* Refresh writes a new 64..127 counter only when the sampled current counter
 * is at least 64 and below the window; BUSY means too early and TIMEOUT means
 * already below 64. The counter keeps decrementing while this runs, so even
 * OK cannot guarantee avoiding reset at the boundary: schedule with margin.
 * The caller serializes access, owns the clock and accounts for its changes. */
ABoxHalStatus ABoxWwdg_Refresh(uint8_t counter);
ABoxHalStatus ABoxWwdg_ReadCounter(uint8_t *counter);
void ABoxWwdg_Irq(void);
/* Controls only the debug-halt freeze bit; it does not stop the watchdog in
 * ordinary operation. This changes timing while a debugger halts the core. */
ABoxHalStatus ABoxWwdg_SetDebugFreeze(uint8_t enabled);
#endif
