#include <assert.h>
#include "abox_low_power_backend.h"
static int can_enter=1,entered,restored,apply_calls;static uint8_t flags,pin;
static int on_wake_clock,source_matches=1;
static ABoxHalStatus restore_result=ABOX_HAL_OK;
static ABoxMcuClockFrequencies expected={72000000U,72000000U,36000000U,72000000U,72000000U,72000000U};
static ABoxMcuClockFrequencies actual;
int ABoxLowPowerHw_CanEnter(void) { return can_enter; }
int ABoxLowPowerHw_OnWakeClock(void) { return on_wake_clock; }
int ABoxLowPowerHw_SourceMatches(ABoxMcuClockSource source) { (void)source;return source_matches; }
void ABoxLowPowerHw_Enter(ABoxLowPowerMode mode,ABoxLowPowerWait wait,uint8_t low)
{ assert(mode<=ABOX_LOW_POWER_STANDBY && wait<=ABOX_LOW_POWER_WFE && low<=1U);++entered; }
uint8_t ABoxLowPowerHw_Flags(void) { return flags; }
void ABoxLowPowerHw_ClearFlags(uint8_t value) { flags &= (uint8_t)~value; }
void ABoxLowPowerHw_SetWakePin(uint8_t value) { pin=value; }
ABoxHalStatus ABoxMcuClock_Calculate(const ABoxMcuClockConfig *c,ABoxMcuClockFrequencies *out)
{ if (!c) return ABOX_HAL_INVALID;*out=expected;return ABOX_HAL_OK; }
ABoxHalStatus ABoxMcuClock_Get(ABoxMcuClockFrequencies *out) { *out=actual;return ABOX_HAL_OK; }
ABoxHalStatus ABoxMcuClock_Apply(const ABoxMcuClockConfig *c) { (void)c;++apply_calls;return restore_result; }
static ABoxHalStatus restore(void *ctx) { assert(ctx==(void *)0x1234);++restored;return restore_result; }
int main(void)
{
    ABoxLowPowerConfig c={ABOX_LOW_POWER_STOP,ABOX_LOW_POWER_WFI,1U,(void *)0x1234,restore};
    ABoxMcuClockConfig clock={0};uint8_t out=0U;
    assert(ABoxLowPower_Enter(0)==ABOX_HAL_INVALID);
    c.restore=0;assert(ABoxLowPower_Enter(&c)==ABOX_HAL_INVALID && !entered);c.restore=restore;
    can_enter=0;assert(ABoxLowPower_Enter(&c)==ABOX_HAL_BUSY && !entered);can_enter=1;
    assert(ABoxLowPower_Enter(&c)==ABOX_HAL_OK && entered==1 && restored==1);
    restore_result=ABOX_HAL_ERROR;assert(ABoxLowPower_Enter(&c)==ABOX_HAL_ERROR && restored==2);
    c.mode=ABOX_LOW_POWER_STANDBY;c.low_power_regulator=0U;
    assert(ABoxLowPower_Enter(&c)==ABOX_HAL_ERROR && restored==2);
    flags=3U;assert(ABoxLowPower_GetFlags(&out)==ABOX_HAL_OK && out==3U);
    assert(ABoxLowPower_ClearFlags(1U)==ABOX_HAL_OK && flags==2U);
    assert(ABoxLowPower_ClearFlags(4U)==ABOX_HAL_INVALID);
    assert(ABoxLowPower_SetWakePin(1U)==ABOX_HAL_OK && pin);
    assert(ABoxLowPower_SetWakePin(2U)==ABOX_HAL_INVALID);
    actual=expected;assert(ABoxLowPower_RestoreClock(&clock)==ABOX_HAL_OK && !apply_calls);
    clock.source=ABOX_MCU_CLOCK_HSE;on_wake_clock=1;source_matches=0;restore_result=ABOX_HAL_OK;
    assert(ABoxLowPower_RestoreClock(&clock)==ABOX_HAL_OK && apply_calls==1);
    on_wake_clock=0;assert(ABoxLowPower_RestoreClock(&clock)==ABOX_HAL_BUSY && apply_calls==1);
    source_matches=1;restore_result=ABOX_HAL_ERROR;
    actual.system_hz=8000000U;assert(ABoxLowPower_RestoreClock(&clock)==ABOX_HAL_ERROR && apply_calls==2);
    return 0;
}
