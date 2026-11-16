#include "abox_low_power_backend.h"
ABoxHalStatus ABoxLowPower_Enter(const ABoxLowPowerConfig *c)
{
    if (!c || (unsigned)c->mode>ABOX_LOW_POWER_STANDBY || (unsigned)c->wait>ABOX_LOW_POWER_WFE ||
        c->low_power_regulator>1U || (c->mode!=ABOX_LOW_POWER_STOP && c->low_power_regulator) ||
        (c->mode==ABOX_LOW_POWER_STOP && !c->restore)) return ABOX_HAL_INVALID;
    if (!ABoxLowPowerHw_CanEnter()) return ABOX_HAL_BUSY;
    ABoxLowPowerHw_Enter(c->mode,c->wait,c->low_power_regulator);
    if (c->mode==ABOX_LOW_POWER_STANDBY) return ABOX_HAL_ERROR;
    return c->mode==ABOX_LOW_POWER_STOP ? c->restore(c->context) : ABOX_HAL_OK;
}
ABoxHalStatus ABoxLowPower_RestoreClock(const ABoxMcuClockConfig *config)
{
    ABoxMcuClockFrequencies expected,actual;ABoxHalStatus result;
    result=ABoxMcuClock_Calculate(config,&expected);if (result!=ABOX_HAL_OK) return result;
    /* Equal frequencies do not establish an equal source: HSI and HSE may
     * both be 8 MHz. Restore the selected source after STOP even in that case. */
    if (!ABoxLowPowerHw_SourceMatches(config->source))
        return ABoxLowPowerHw_OnWakeClock() ? ABoxMcuClock_Apply(config) : ABOX_HAL_BUSY;
    result=ABoxMcuClock_Get(&actual);if (result!=ABOX_HAL_OK) return result;
    if (actual.system_hz==expected.system_hz && actual.ahb_hz==expected.ahb_hz &&
        actual.apb1_hz==expected.apb1_hz && actual.apb2_hz==expected.apb2_hz) return ABOX_HAL_OK;
    return ABoxMcuClock_Apply(config);
}
ABoxHalStatus ABoxLowPower_GetFlags(uint8_t *flags)
{ if (!flags) return ABOX_HAL_INVALID;*flags=ABoxLowPowerHw_Flags();return ABOX_HAL_OK; }
ABoxHalStatus ABoxLowPower_ClearFlags(uint8_t flags)
{ if (!flags || (flags & ~3U)) return ABOX_HAL_INVALID;ABoxLowPowerHw_ClearFlags(flags);return ABOX_HAL_OK; }
ABoxHalStatus ABoxLowPower_SetWakePin(uint8_t enabled)
{ if (enabled>1U) return ABOX_HAL_INVALID;ABoxLowPowerHw_SetWakePin(enabled);return ABOX_HAL_OK; }
