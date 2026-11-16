#include "abox_rtc_backend.h"
#include "abox_platform_port.h"
static uint8_t opened, interrupts;
static int tick_ready(void)
{ const ABoxPlatformPort *p=ABox_PlatformPortGet(); return p && p->get_tick_ms; }
static int wait_ready(int (*ready)(void), uint32_t start, uint32_t timeout)
{
    do {
        if (ready()) return 1;
    } while ((uint32_t)(ABox_PortGetTickMs()-start)<timeout);
    return 0;
}
static ABoxRtcSource waiting_source;
static int oscillator_ready(void) { return ABoxRtcHw_OscillatorReady(waiting_source); }
ABoxHalStatus ABoxRtc_Open(const ABoxRtcConfig *config,uint32_t timeout_ms)
{
    uint32_t key,start,pclk; unsigned existing; ABoxRtcSource hardware_source;ABoxHalStatus result=ABOX_HAL_OK;
    if (!config || !tick_ready() || config->source<ABOX_RTC_LSE || config->source>ABOX_RTC_LSE_BYPASS ||
        !config->source_hz || !config->divider || config->divider>1048576U || config->initialize_if_empty>1U)
        return ABOX_HAL_INVALID;
    if (config->source==ABOX_RTC_LSE_BYPASS && (config->source_hz<32768U || config->source_hz>1000000U))
        return ABOX_HAL_INVALID;
    if (opened) return ABOX_HAL_BUSY;
    pclk=ABoxRtcHw_Apb1Hz();
    if (!pclk || config->source_hz>pclk/4U) return ABOX_HAL_UNSUPPORTED;
    key=ABoxRtcHw_AccessBegin(); start=ABox_PortGetTickMs();
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms)) { result=ABOX_HAL_TIMEOUT; goto done; }
    existing=ABoxRtcHw_Source();hardware_source=config->source==ABOX_RTC_LSE_BYPASS ? ABOX_RTC_LSE : config->source;
    if ((existing && existing!=(unsigned)hardware_source) || (!existing && !config->initialize_if_empty)) {
        result=ABOX_HAL_BUSY; goto done;
    }
    if (hardware_source==ABOX_RTC_LSE) {
        result=ABoxRtcHw_SetLseMode(config->source==ABOX_RTC_LSE_BYPASS);
        if (result!=ABOX_HAL_OK) goto done;
    }
    waiting_source=hardware_source; ABoxRtcHw_StartOscillator(hardware_source);
    if (!wait_ready(oscillator_ready,start,timeout_ms)) { result=ABOX_HAL_TIMEOUT; goto done; }
    if (!existing) ABoxRtcHw_SelectSource(hardware_source);
    ABoxRtcHw_EnableClock();
    if (!wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { result=ABOX_HAL_TIMEOUT; goto done; }
    ABoxRtcHw_BeginSync();
    if (!wait_ready(ABoxRtcHw_SyncReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) {
        result=ABOX_HAL_TIMEOUT; goto done;
    }
    if (!existing) {
        ABoxRtcHw_BeginConfig(); ABoxRtcHw_WritePrescaler(config->divider-1U);
        ABoxRtcHw_WriteCounter(config->initial_counter); ABoxRtcHw_EndConfig();
        if (!wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { result=ABOX_HAL_TIMEOUT; goto done; }
    } else if (ABoxRtcHw_ReadPrescaler()!=config->divider-1U) { result=ABOX_HAL_BUSY; goto done; }
    opened=1U; interrupts=0U;
done:
    ABoxRtcHw_AccessEnd(key); return result;
}
ABoxHalStatus ABoxRtc_Close(uint32_t timeout_ms)
{
    uint32_t key,start;
    if (!opened || !tick_ready()) return ABOX_HAL_INVALID;
    if (interrupts) {
        key=ABoxRtcHw_AccessBegin(); start=ABox_PortGetTickMs();
        if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
        ABoxRtcHw_SetInterrupts(0U);
        if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
        ABoxRtcHw_AccessEnd(key);
    }
    opened=0U; interrupts=0U; return ABOX_HAL_OK;
}
ABoxHalStatus ABoxRtc_Read(uint32_t *counter)
{
    uint32_t high,low,next;
    if (!opened || !counter) return ABOX_HAL_INVALID;
    for (unsigned i=0;i<3U;++i) {
        high=ABoxRtcHw_ReadCounterHigh(); low=ABoxRtcHw_ReadCounterLow(); next=ABoxRtcHw_ReadCounterHigh();
        if (high==next) { *counter=(high<<16U)|low; return ABOX_HAL_OK; }
    }
    return ABOX_HAL_ERROR;
}
static ABoxHalStatus write_value(uint32_t value,uint32_t timeout_ms,int alarm)
{
    uint32_t key,start;
    if (!opened || !tick_ready()) return ABOX_HAL_INVALID;
    key=ABoxRtcHw_AccessBegin(); start=ABox_PortGetTickMs();
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_BeginConfig(); if (alarm) ABoxRtcHw_WriteAlarm(value); else ABoxRtcHw_WriteCounter(value);
    ABoxRtcHw_EndConfig();
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_AccessEnd(key); return ABOX_HAL_OK;
}
ABoxHalStatus ABoxRtc_Write(uint32_t counter,uint32_t timeout_ms) { return write_value(counter,timeout_ms,0); }
ABoxHalStatus ABoxRtc_SetAlarm(uint32_t counter,uint32_t timeout_ms) { return write_value(counter,timeout_ms,1); }
ABoxHalStatus ABoxRtc_GetFlags(uint8_t *flags)
{ if (!opened || !flags) return ABOX_HAL_INVALID; *flags=ABoxRtcHw_Flags(); return ABOX_HAL_OK; }
ABoxHalStatus ABoxRtc_ClearFlags(uint8_t flags,uint32_t timeout_ms)
{
    uint32_t key,start;
    if (!opened || !tick_ready() || !flags || (flags & ~7U)) return ABOX_HAL_INVALID;
    key=ABoxRtcHw_AccessBegin(); start=ABox_PortGetTickMs();
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_ClearFlags(flags);
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_AccessEnd(key); return ABOX_HAL_OK;
}
ABoxHalStatus ABoxRtc_SetInterrupts(uint8_t mask,uint32_t timeout_ms)
{
    uint32_t key,start;
    if (!opened || !tick_ready() || (mask & ~7U)) return ABOX_HAL_INVALID;
    key=ABoxRtcHw_AccessBegin(); start=ABox_PortGetTickMs();
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_SetInterrupts(mask); interrupts=mask;
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms) || !wait_ready(ABoxRtcHw_WriteReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_AccessEnd(key); return ABOX_HAL_OK;
}
ABoxHalStatus ABoxRtc_DispatchEvents(const ABoxRtcEvents *events,uint32_t timeout_ms)
{
    uint8_t pending;
    if (!opened || !tick_ready() || !events || !events->on_events) return ABOX_HAL_INVALID;
    pending=(uint8_t)(ABoxRtcHw_Flags() & interrupts);
    if (!pending) return ABOX_HAL_OK;
    events->on_events(events->context,pending);
    return ABoxRtc_ClearFlags(pending,timeout_ms);
}
ABoxHalStatus ABoxRtc_ResetBackupDomain(uint32_t timeout_ms)
{
    uint32_t key,start; if (opened) return ABOX_HAL_BUSY;
    if (!tick_ready()) return ABOX_HAL_INVALID;
    key=ABoxRtcHw_AccessBegin(); start=ABox_PortGetTickMs();
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_ResetBackup(); ABoxRtcHw_AccessEnd(key); return ABOX_HAL_OK;
}
ABoxHalStatus ABoxRtc_GetCalibration(uint8_t *pulses)
{
    if (!opened || !pulses) return ABOX_HAL_INVALID;
    *pulses=ABoxRtcHw_ReadCalibration(); return ABOX_HAL_OK;
}
ABoxHalStatus ABoxRtc_SetCalibration(uint8_t pulses,uint32_t timeout_ms)
{
    uint32_t key,start; ABoxHalStatus result;
    if (!opened || !tick_ready() || pulses>127U) return ABOX_HAL_INVALID;
    key=ABoxRtcHw_AccessBegin(); start=ABox_PortGetTickMs();
    if (!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms)) { ABoxRtcHw_AccessEnd(key); return ABOX_HAL_TIMEOUT; }
    ABoxRtcHw_WriteCalibration(pulses);
    result=ABoxRtcHw_ReadCalibration()==pulses ? ABOX_HAL_OK : ABOX_HAL_ERROR;
    ABoxRtcHw_AccessEnd(key); return result;
}
static ABoxRtcOutput rtc_output(uint32_t bits)
{
    if(bits & (1U<<8))return (bits & (1U<<9)) ? ABOX_RTC_OUTPUT_SECOND : ABOX_RTC_OUTPUT_ALARM;
    return (bits & (1U<<7)) ? ABOX_RTC_OUTPUT_CALIBRATION : ABOX_RTC_OUTPUT_OFF;
}
ABoxHalStatus ABoxRtc_GetOutput(ABoxRtcOutput *output)
{
    if(!opened || !output)return ABOX_HAL_INVALID;
    *output=rtc_output(ABoxRtcHw_ReadOutputBits());return ABOX_HAL_OK;
}
ABoxHalStatus ABoxRtc_SetOutput(ABoxRtcOutput output,uint32_t timeout_ms)
{
    uint32_t key,start,bits;ABoxHalStatus result;
    if(!opened || !tick_ready() || (unsigned)output>ABOX_RTC_OUTPUT_SECOND)return ABOX_HAL_INVALID;
    bits=output==ABOX_RTC_OUTPUT_CALIBRATION ? (1U<<7) : output==ABOX_RTC_OUTPUT_ALARM ? (1U<<8) :
         output==ABOX_RTC_OUTPUT_SECOND ? (3U<<8) : 0U;
    key=ABoxRtcHw_AccessBegin();start=ABox_PortGetTickMs();
    if(!wait_ready(ABoxRtcHw_AccessReady,start,timeout_ms)){ABoxRtcHw_AccessEnd(key);return ABOX_HAL_TIMEOUT;}
    ABoxRtcHw_WriteOutputBits(bits);
    result=(ABoxRtcHw_ReadOutputBits() & 0x380U)==bits ? ABOX_HAL_OK : ABOX_HAL_ERROR;
    ABoxRtcHw_AccessEnd(key);return result;
}
