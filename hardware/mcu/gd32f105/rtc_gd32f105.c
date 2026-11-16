#include "abox_rtc_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_pmu.h"
#include "gd32f10x_rtc.h"
uint32_t ABoxRtcHw_AccessBegin(void)
{
    uint32_t previous;
    rcu_periph_clock_enable(RCU_PMU); rcu_periph_clock_enable(RCU_BKPI);
    previous=PMU_CTL & PMU_CTL_BKPWEN; PMU_CTL |= PMU_CTL_BKPWEN; return previous;
}
void ABoxRtcHw_AccessEnd(uint32_t key) { if (!key) PMU_CTL &= ~PMU_CTL_BKPWEN; }
int ABoxRtcHw_AccessReady(void) { return (PMU_CTL & PMU_CTL_BKPWEN)!=0U; }
uint32_t ABoxRtcHw_Apb1Hz(void) { return rcu_clock_freq_get(CK_APB1); }
unsigned ABoxRtcHw_Source(void) { return (unsigned)((RCU_BDCTL & RCU_BDCTL_RTCSRC)>>8U); }
ABoxHalStatus ABoxRtcHw_SetLseMode(uint8_t bypass)
{
    if ((RCU_BDCTL & RCU_BDCTL_LXTALEN) && !!(RCU_BDCTL & RCU_BDCTL_LXTALBPS)!=!!bypass) return ABOX_HAL_BUSY;
    if (!(RCU_BDCTL & RCU_BDCTL_LXTALEN)) {
        if (bypass) RCU_BDCTL |= RCU_BDCTL_LXTALBPS; else RCU_BDCTL &= ~RCU_BDCTL_LXTALBPS;
    }
    return ABOX_HAL_OK;
}
void ABoxRtcHw_StartOscillator(ABoxRtcSource source)
{
    if (source==ABOX_RTC_LSE) RCU_BDCTL |= RCU_BDCTL_LXTALEN;
    else if (source==ABOX_RTC_LSI) RCU_RSTSCK |= RCU_RSTSCK_IRC40KEN;
}
int ABoxRtcHw_OscillatorReady(ABoxRtcSource source)
{
    return source==ABOX_RTC_LSE ? (RCU_BDCTL & RCU_BDCTL_LXTALSTB)!=0U :
           source==ABOX_RTC_LSI ? (RCU_RSTSCK & RCU_RSTSCK_IRC40KSTB)!=0U : (RCU_CTL & RCU_CTL_HXTALSTB)!=0U;
}
void ABoxRtcHw_SelectSource(ABoxRtcSource source) { RCU_BDCTL |= (uint32_t)source<<8U; }
void ABoxRtcHw_EnableClock(void) { RCU_BDCTL |= RCU_BDCTL_RTCEN; }
void ABoxRtcHw_BeginSync(void) { RTC_CTL=RTC_CTL_SCIF|RTC_CTL_ALRMIF|RTC_CTL_OVIF; }
int ABoxRtcHw_SyncReady(void) { return (RTC_CTL & RTC_CTL_RSYNF)!=0U; }
int ABoxRtcHw_WriteReady(void) { return (RTC_CTL & RTC_CTL_LWOFF)!=0U; }
void ABoxRtcHw_BeginConfig(void) { RTC_CTL=RTC_CTL_CMF|RTC_CTL_RSYNF|RTC_CTL_SCIF|RTC_CTL_ALRMIF|RTC_CTL_OVIF; }
void ABoxRtcHw_EndConfig(void) { RTC_CTL=RTC_CTL_RSYNF|RTC_CTL_SCIF|RTC_CTL_ALRMIF|RTC_CTL_OVIF; }
uint32_t ABoxRtcHw_ReadPrescaler(void) { return ((RTC_PSCH & 15U)<<16U)|(RTC_PSCL & 65535U); }
uint32_t ABoxRtcHw_ReadCounterHigh(void) { return RTC_CNTH & 65535U; }
uint32_t ABoxRtcHw_ReadCounterLow(void) { return RTC_CNTL & 65535U; }
void ABoxRtcHw_WritePrescaler(uint32_t v) { RTC_PSCH=v>>16U; RTC_PSCL=v&65535U; }
void ABoxRtcHw_WriteCounter(uint32_t v) { RTC_CNTH=v>>16U; RTC_CNTL=v&65535U; }
void ABoxRtcHw_WriteAlarm(uint32_t v) { RTC_ALRMH=v>>16U; RTC_ALRML=v&65535U; }
uint8_t ABoxRtcHw_Flags(void) { return (uint8_t)(RTC_CTL & 7U); }
void ABoxRtcHw_ClearFlags(uint8_t flags) { RTC_CTL=(RTC_CTL & RTC_CTL_CMF)|RTC_CTL_RSYNF|(7U & ~(uint32_t)flags); }
void ABoxRtcHw_SetInterrupts(uint8_t mask) { RTC_INTEN=mask & 7U; }
/* BKP_OCTL at offset 0x2c is not defined by the vendored GD headers. */
#define ABOX_GD_BKP_OCTL REG32(BKP_BASE+0x2cU)
uint8_t ABoxRtcHw_ReadCalibration(void) { return (uint8_t)(ABOX_GD_BKP_OCTL & 0x7fU); }
void ABoxRtcHw_WriteCalibration(uint8_t pulses)
{ ABOX_GD_BKP_OCTL=(ABOX_GD_BKP_OCTL & ~0x7fU)|pulses; }
uint32_t ABoxRtcHw_ReadOutputBits(void) { return ABOX_GD_BKP_OCTL & 0x380U; }
void ABoxRtcHw_WriteOutputBits(uint32_t bits) { ABOX_GD_BKP_OCTL=(ABOX_GD_BKP_OCTL & ~0x380U)|(bits & 0x380U); }
void ABoxRtcHw_ResetBackup(void) { RCU_BDCTL |= RCU_BDCTL_BKPRST; RCU_BDCTL &= ~RCU_BDCTL_BKPRST; }
