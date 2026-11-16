#ifndef ABOX_TIME_H
#define ABOX_TIME_H
#include <stdint.h>
#include "abox_hal_status.h"

/* Reads use the time source bound by the platform/board. TickMs wraps at
 * 2^32 ms; subtract unsigned values for intervals shorter than one wrap.
 * MonotonicMs is a separate optional 64-bit uptime, not wall-clock UTC. */
ABoxHalStatus ABoxTime_TickMs(uint32_t *value);
ABoxHalStatus ABoxTime_MonotonicMs(uint64_t *value);
/* Delay is a blocking wait and requires the selected tick to advance; never
 * call while masking the interrupt or task that provides that tick. */
ABoxHalStatus ABoxTime_DelayMs(uint32_t delay_ms);
#endif
