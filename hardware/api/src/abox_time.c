#include "abox_time.h"
#include "abox_platform_port.h"

ABoxHalStatus ABoxTime_TickMs(uint32_t *value)
{
    const ABoxPlatformPort *p = ABox_PlatformPortGet();
    if (!value) return ABOX_HAL_INVALID;
    if (!p || !p->get_tick_ms) return ABOX_HAL_UNSUPPORTED;
    *value = p->get_tick_ms(p->context);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTime_MonotonicMs(uint64_t *value)
{
    const ABoxPlatformPort *p = ABox_PlatformPortGet();
    if (!value) return ABOX_HAL_INVALID;
    if (!p || !p->get_monotonic_ms) return ABOX_HAL_UNSUPPORTED;
    *value = p->get_monotonic_ms(p->context);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxTime_DelayMs(uint32_t delay_ms)
{
    uint32_t start, now;
    ABoxHalStatus status = ABoxTime_TickMs(&start);
    if (status != ABOX_HAL_OK) return status;
    do {
        status = ABoxTime_TickMs(&now);
        if (status != ABOX_HAL_OK) return status;
    } while ((uint32_t)(now - start) < delay_ms);
    return ABOX_HAL_OK;
}
