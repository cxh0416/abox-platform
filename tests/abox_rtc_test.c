#include <assert.h>
#include "abox_rtc_backend.h"
#include "abox_platform_port.h"
static uint32_t now,source,prescaler,counter,alarm,flags,irqs,resets;
static uint8_t calibration;static uint32_t output_bits;
static uint8_t lse_on,lse_bypass;
static int osc_ready=1,sync_ready=1,write_ready=1,access_ready=1,tick_bound=1;
static unsigned selected,configured,rollover,high_reads;
static unsigned event_calls;static uint8_t delivered;
static void events(void *context,uint8_t mask) { assert(context==(void *)0x1234);++event_calls;delivered=mask; }
static uint32_t tick(void *context) { (void)context;return now++; }
static ABoxPlatformPort port={.get_tick_ms=tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return tick_bound ? &port : 0; }
uint32_t ABox_PortGetTickMs(void) { return now++; }
uint32_t ABoxRtcHw_AccessBegin(void) { return 42U; }
void ABoxRtcHw_AccessEnd(uint32_t key) { assert(key==42U); }
int ABoxRtcHw_AccessReady(void) { return access_ready; }
uint32_t ABoxRtcHw_Apb1Hz(void) { return 36000000U; }
unsigned ABoxRtcHw_Source(void) { return source; }
ABoxHalStatus ABoxRtcHw_SetLseMode(uint8_t bypass)
{ if(lse_on && lse_bypass!=bypass)return ABOX_HAL_BUSY;lse_bypass=bypass;return ABOX_HAL_OK; }
void ABoxRtcHw_StartOscillator(ABoxRtcSource s) { if(s==ABOX_RTC_LSE)lse_on=1U; }
int ABoxRtcHw_OscillatorReady(ABoxRtcSource s) { (void)s;return osc_ready; }
void ABoxRtcHw_SelectSource(ABoxRtcSource s) { source=s;++selected; }
void ABoxRtcHw_EnableClock(void) {}
void ABoxRtcHw_BeginSync(void) {}
int ABoxRtcHw_SyncReady(void) { return sync_ready; }
int ABoxRtcHw_WriteReady(void) { return write_ready; }
void ABoxRtcHw_BeginConfig(void) { ++configured; }
void ABoxRtcHw_EndConfig(void) {}
uint32_t ABoxRtcHw_ReadPrescaler(void) { return prescaler; }
uint32_t ABoxRtcHw_ReadCounterHigh(void)
{ if(rollover && high_reads++==0U)return 0U; return counter>>16U; }
uint32_t ABoxRtcHw_ReadCounterLow(void) { return counter&65535U; }
void ABoxRtcHw_WritePrescaler(uint32_t v) { prescaler=v; }
void ABoxRtcHw_WriteCounter(uint32_t v) { counter=v; }
void ABoxRtcHw_WriteAlarm(uint32_t v) { alarm=v; }
uint8_t ABoxRtcHw_Flags(void) { return (uint8_t)flags; }
void ABoxRtcHw_ClearFlags(uint8_t f) { flags&=~f; }
void ABoxRtcHw_SetInterrupts(uint8_t f) { irqs=f; }
uint8_t ABoxRtcHw_ReadCalibration(void) { return calibration; }
void ABoxRtcHw_WriteCalibration(uint8_t value) { calibration=value; }
uint32_t ABoxRtcHw_ReadOutputBits(void) { return output_bits; }
void ABoxRtcHw_WriteOutputBits(uint32_t value) { output_bits=value; }
void ABoxRtcHw_ResetBackup(void) { ++resets;source=prescaler=counter=alarm=flags=0;lse_on=lse_bypass=0U; }
int main(void)
{
 ABoxRtcConfig c={ABOX_RTC_LSE,32768U,32768U,100U,0U};uint32_t value=77U;uint8_t f=0;
 assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_BUSY && !selected && !configured);
 c.initialize_if_empty=1U;osc_ready=0;assert(ABoxRtc_Open(&c,3U)==ABOX_HAL_TIMEOUT && !selected);
 osc_ready=1;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_OK && source==ABOX_RTC_LSE && prescaler==32767U && counter==100U);
 assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_BUSY && ABoxRtc_ResetBackupDomain(10U)==ABOX_HAL_BUSY && !resets);
 assert(ABoxRtc_SetCalibration(128U,10U)==ABOX_HAL_INVALID);
 assert(ABoxRtc_SetCalibration(127U,10U)==ABOX_HAL_OK);
 assert(ABoxRtc_GetCalibration(&f)==ABOX_HAL_OK && f==127U);
 access_ready=0;assert(ABoxRtc_SetCalibration(0U,2U)==ABOX_HAL_TIMEOUT && calibration==127U);access_ready=1;
 assert(ABoxRtc_SetCalibration(0U,10U)==ABOX_HAL_OK && !calibration);
 {
  ABoxRtcOutput o=ABOX_RTC_OUTPUT_OFF;
  assert(ABoxRtc_SetOutput(ABOX_RTC_OUTPUT_CALIBRATION,10U)==ABOX_HAL_OK && output_bits==0x80U);
  assert(ABoxRtc_GetOutput(&o)==ABOX_HAL_OK && o==ABOX_RTC_OUTPUT_CALIBRATION);
  assert(ABoxRtc_SetOutput(ABOX_RTC_OUTPUT_SECOND,10U)==ABOX_HAL_OK && output_bits==0x300U);
  assert(ABoxRtc_GetOutput(&o)==ABOX_HAL_OK && o==ABOX_RTC_OUTPUT_SECOND);
  output_bits=0x180U;assert(ABoxRtc_GetOutput(&o)==ABOX_HAL_OK && o==ABOX_RTC_OUTPUT_ALARM);
  access_ready=0;assert(ABoxRtc_SetOutput(ABOX_RTC_OUTPUT_OFF,2U)==ABOX_HAL_TIMEOUT && output_bits==0x180U);access_ready=1;
  assert(ABoxRtc_SetOutput(ABOX_RTC_OUTPUT_OFF,10U)==ABOX_HAL_OK && !output_bits);
 }
 assert(ABoxRtc_Read(&value)==ABOX_HAL_OK && value==100U);
 counter=0x00010005U;rollover=1;high_reads=0;assert(ABoxRtc_Read(&value)==ABOX_HAL_OK && value==counter);rollover=0;
 assert(ABoxRtc_Write(0xabcdef01U,10U)==ABOX_HAL_OK && counter==0xabcdef01U);
 assert(ABoxRtc_SetAlarm(123U,10U)==ABOX_HAL_OK && alarm==123U);
 flags=7U;assert(ABoxRtc_GetFlags(&f)==ABOX_HAL_OK && f==7U);
 assert(ABoxRtc_ClearFlags(ABOX_RTC_ALARM,10U)==ABOX_HAL_OK && flags==5U);
 assert(ABoxRtc_SetInterrupts(ABOX_RTC_SECOND|ABOX_RTC_ALARM,10U)==ABOX_HAL_OK && irqs==3U);
 {
  ABoxRtcEvents e={(void *)0x1234,events};
  flags=7U;assert(ABoxRtc_DispatchEvents(&e,10U)==ABOX_HAL_OK && event_calls==1U && delivered==3U && flags==4U);
  assert(ABoxRtc_DispatchEvents(&e,10U)==ABOX_HAL_OK && event_calls==1U && flags==4U);
  flags|=ABOX_RTC_ALARM;access_ready=0;
  assert(ABoxRtc_DispatchEvents(&e,2U)==ABOX_HAL_TIMEOUT && event_calls==2U && flags==6U);
  access_ready=1;assert(ABoxRtc_DispatchEvents(&e,10U)==ABOX_HAL_OK && event_calls==3U && flags==4U);
  assert(ABoxRtc_DispatchEvents(NULL,10U)==ABOX_HAL_INVALID);
 }
 assert(ABoxRtc_SetInterrupts(8U,10U)==ABOX_HAL_INVALID && irqs==3U);
 write_ready=0;assert(ABoxRtc_Write(5U,2U)==ABOX_HAL_TIMEOUT && counter==0xabcdef01U);write_ready=1;
 assert(ABoxRtc_Close(10U)==ABOX_HAL_OK && !irqs && counter==0xabcdef01U);
 c.source=ABOX_RTC_LSI;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_BUSY && counter==0xabcdef01U);
 c.source=ABOX_RTC_LSE;c.divider=32767U;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_BUSY && counter==0xabcdef01U);
 c.divider=32768U;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_OK && counter==0xabcdef01U && configured==3U);
 assert(ABoxRtc_Close(10U)==ABOX_HAL_OK);
 access_ready=0;assert(ABoxRtc_ResetBackupDomain(2U)==ABOX_HAL_TIMEOUT && !resets);access_ready=1;
 tick_bound=0;assert(ABoxRtc_ResetBackupDomain(2U)==ABOX_HAL_INVALID);tick_bound=1;
 assert(ABoxRtc_ResetBackupDomain(10U)==ABOX_HAL_OK && resets==1U && !source);
 c.source_hz=10000000U;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_UNSUPPORTED);
 c.source=ABOX_RTC_LSE_BYPASS;c.source_hz=32767U;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_INVALID);
 c.source_hz=32768U;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_OK && source==ABOX_RTC_LSE && lse_bypass);
 assert(ABoxRtc_Close(10U)==ABOX_HAL_OK);
 c.source=ABOX_RTC_LSE;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_BUSY && counter==100U);
 c.source=ABOX_RTC_LSE_BYPASS;assert(ABoxRtc_Open(&c,10U)==ABOX_HAL_OK && counter==100U);
 assert(ABoxRtc_Close(10U)==ABOX_HAL_OK);
 return 0;
}
