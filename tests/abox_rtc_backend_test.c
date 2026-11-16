#include <assert.h>
#include "abox_rtc_backend.h"
unsigned pwr_clock,bkp_clock;
#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
PwrRegs pwr_regs;RccRegs rcc_regs;RtcRegs rtc_regs;BkpRegs bkp_regs;
#define CALIBRATION bkp_regs.RTCCR
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 36000000U; }
#define POWER pwr_regs.CR
#define BACKUP rcc_regs.BDCR
#define LSI rcc_regs.CSR
#define HSE rcc_regs.CR
#define CONTROL rtc_regs.CRL
#define IRQS rtc_regs.CRH
#define PSCH rtc_regs.PRLH
#define PSCL rtc_regs.PRLL
#define CNTH rtc_regs.CNTH
#define CNTL rtc_regs.CNTL
#define ALRH rtc_regs.ALRH
#define ALRL rtc_regs.ALRL
#else
#include "gd32f10x.h"
#include "gd32f10x_pmu.h"
#include "gd32f10x_rtc.h"
uint32_t gd_bdctl,gd_rstsck,gd_ctl,gd_pmu,gd_rtc_inten,gd_rtc_ctl,gd_psch,gd_pscl,gd_cnth,gd_cntl,gd_alarmh,gd_alarml;
uint32_t gd_bkp_octl;
#define CALIBRATION gd_bkp_octl
void rcu_periph_clock_enable(uint32_t id) { if(id==RCU_PMU)++pwr_clock;else {assert(id==RCU_BKPI);++bkp_clock;} }
uint32_t rcu_clock_freq_get(uint32_t id) { assert(id==CK_APB1);return 36000000U; }
#define POWER gd_pmu
#define BACKUP gd_bdctl
#define LSI gd_rstsck
#define HSE gd_ctl
#define CONTROL gd_rtc_ctl
#define IRQS gd_rtc_inten
#define PSCH gd_psch
#define PSCL gd_pscl
#define CNTH gd_cnth
#define CNTL gd_cntl
#define ALRH gd_alarmh
#define ALRL gd_alarml
#endif
int main(void)
{
 uint32_t key=ABoxRtcHw_AccessBegin();assert(!key && pwr_clock==1U && bkp_clock==1U && ABoxRtcHw_AccessReady());
 assert(ABoxRtcHw_Apb1Hz()==36000000U && ABoxRtcHw_Source()==0U);
 assert(ABoxRtcHw_SetLseMode(1U)==ABOX_HAL_OK && (BACKUP&4U));
 BACKUP|=1U;assert(ABoxRtcHw_SetLseMode(0U)==ABOX_HAL_BUSY && (BACKUP&4U));
 BACKUP&=~1U;assert(ABoxRtcHw_SetLseMode(0U)==ABOX_HAL_OK && !(BACKUP&4U));
 ABoxRtcHw_StartOscillator(ABOX_RTC_LSE);assert(BACKUP&1U);BACKUP|=2U;assert(ABoxRtcHw_OscillatorReady(ABOX_RTC_LSE));
 ABoxRtcHw_SelectSource(ABOX_RTC_LSE);ABoxRtcHw_EnableClock();assert(ABoxRtcHw_Source()==1U && (BACKUP&(1U<<15)));
 ABoxRtcHw_StartOscillator(ABOX_RTC_LSI);assert(LSI&1U);LSI|=2U;assert(ABoxRtcHw_OscillatorReady(ABOX_RTC_LSI));
 HSE|=1U<<17;assert(ABoxRtcHw_OscillatorReady(ABOX_RTC_HSE_DIV128));
 CONTROL=32U;assert(ABoxRtcHw_WriteReady());ABoxRtcHw_BeginSync();assert(CONTROL==7U);
 CONTROL|=8U;assert(ABoxRtcHw_SyncReady());ABoxRtcHw_BeginConfig();assert(CONTROL==31U);
 ABoxRtcHw_WritePrescaler(0xabcdeU);assert(PSCH==0xaU && PSCL==0xbcdeU && ABoxRtcHw_ReadPrescaler()==0xabcdeU);
 ABoxRtcHw_WriteCounter(0x12345678U);assert(CNTH==0x1234U && CNTL==0x5678U && ABoxRtcHw_ReadCounterHigh()==0x1234U && ABoxRtcHw_ReadCounterLow()==0x5678U);
 ABoxRtcHw_WriteAlarm(0x87654321U);assert(ALRH==0x8765U && ALRL==0x4321U);
 ABoxRtcHw_EndConfig();assert(CONTROL==15U && ABoxRtcHw_Flags()==7U);
 ABoxRtcHw_ClearFlags(2U);assert(CONTROL==13U);
 ABoxRtcHw_SetInterrupts(5U);assert(IRQS==5U);
 CALIBRATION=0x380U;ABoxRtcHw_WriteCalibration(127U);
 assert(CALIBRATION==0x3ffU && ABoxRtcHw_ReadCalibration()==127U);
 ABoxRtcHw_WriteCalibration(0U);assert(CALIBRATION==0x380U);
 ABoxRtcHw_WriteCalibration(55U);ABoxRtcHw_WriteOutputBits(0x100U);
 assert(CALIBRATION==0x137U && ABoxRtcHw_ReadOutputBits()==0x100U && ABoxRtcHw_ReadCalibration()==55U);
 ABoxRtcHw_WriteOutputBits(0x300U);assert(CALIBRATION==0x337U);
 ABoxRtcHw_AccessEnd(key);assert(!(POWER&(1U<<8)));
 POWER|=1U<<8;key=ABoxRtcHw_AccessBegin();ABoxRtcHw_AccessEnd(key);assert(POWER&(1U<<8));
 ABoxRtcHw_ResetBackup();assert(!(BACKUP&(1U<<16)));return 0;
}
