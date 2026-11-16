#include "abox_rtc_backend.h"
#include "stm32f1xx_hal.h"
uint32_t ABoxRtcHw_AccessBegin(void)
{
    uint32_t previous;
    __HAL_RCC_PWR_CLK_ENABLE(); __HAL_RCC_BKP_CLK_ENABLE();
    previous=PWR->CR & PWR_CR_DBP; PWR->CR |= PWR_CR_DBP; return previous;
}
void ABoxRtcHw_AccessEnd(uint32_t key) { if (!key) PWR->CR &= ~PWR_CR_DBP; }
int ABoxRtcHw_AccessReady(void) { return (PWR->CR & PWR_CR_DBP)!=0U; }
uint32_t ABoxRtcHw_Apb1Hz(void) { return HAL_RCC_GetPCLK1Freq(); }
unsigned ABoxRtcHw_Source(void) { return (unsigned)((RCC->BDCR & RCC_BDCR_RTCSEL)>>8U); }
ABoxHalStatus ABoxRtcHw_SetLseMode(uint8_t bypass)
{
    if ((RCC->BDCR & RCC_BDCR_LSEON) && !!(RCC->BDCR & RCC_BDCR_LSEBYP)!=!!bypass) return ABOX_HAL_BUSY;
    if (!(RCC->BDCR & RCC_BDCR_LSEON)) {
        if (bypass) RCC->BDCR |= RCC_BDCR_LSEBYP; else RCC->BDCR &= ~RCC_BDCR_LSEBYP;
    }
    return ABOX_HAL_OK;
}
void ABoxRtcHw_StartOscillator(ABoxRtcSource source)
{
    if (source==ABOX_RTC_LSE) RCC->BDCR |= RCC_BDCR_LSEON;
    else if (source==ABOX_RTC_LSI) RCC->CSR |= RCC_CSR_LSION;
}
int ABoxRtcHw_OscillatorReady(ABoxRtcSource source)
{
    return source==ABOX_RTC_LSE ? (RCC->BDCR & RCC_BDCR_LSERDY)!=0U :
           source==ABOX_RTC_LSI ? (RCC->CSR & RCC_CSR_LSIRDY)!=0U : (RCC->CR & RCC_CR_HSERDY)!=0U;
}
void ABoxRtcHw_SelectSource(ABoxRtcSource source) { RCC->BDCR |= (uint32_t)source<<8U; }
void ABoxRtcHw_EnableClock(void) { RCC->BDCR |= RCC_BDCR_RTCEN; }
void ABoxRtcHw_BeginSync(void) { RTC->CRL=RTC_CRL_SECF|RTC_CRL_ALRF|RTC_CRL_OWF; }
int ABoxRtcHw_SyncReady(void) { return (RTC->CRL & RTC_CRL_RSF)!=0U; }
int ABoxRtcHw_WriteReady(void) { return (RTC->CRL & RTC_CRL_RTOFF)!=0U; }
void ABoxRtcHw_BeginConfig(void) { RTC->CRL=RTC_CRL_CNF|RTC_CRL_RSF|RTC_CRL_SECF|RTC_CRL_ALRF|RTC_CRL_OWF; }
void ABoxRtcHw_EndConfig(void) { RTC->CRL=RTC_CRL_RSF|RTC_CRL_SECF|RTC_CRL_ALRF|RTC_CRL_OWF; }
uint32_t ABoxRtcHw_ReadPrescaler(void) { return ((RTC->PRLH & 15U)<<16U)|(RTC->PRLL & 65535U); }
uint32_t ABoxRtcHw_ReadCounterHigh(void) { return RTC->CNTH & 65535U; }
uint32_t ABoxRtcHw_ReadCounterLow(void) { return RTC->CNTL & 65535U; }
void ABoxRtcHw_WritePrescaler(uint32_t v) { RTC->PRLH=v>>16U; RTC->PRLL=v&65535U; }
void ABoxRtcHw_WriteCounter(uint32_t v) { RTC->CNTH=v>>16U; RTC->CNTL=v&65535U; }
void ABoxRtcHw_WriteAlarm(uint32_t v) { RTC->ALRH=v>>16U; RTC->ALRL=v&65535U; }
uint8_t ABoxRtcHw_Flags(void) { return (uint8_t)(RTC->CRL & 7U); }
void ABoxRtcHw_ClearFlags(uint8_t flags)
{ RTC->CRL=(RTC->CRL & RTC_CRL_CNF)|RTC_CRL_RSF|(7U & ~(uint32_t)flags); }
void ABoxRtcHw_SetInterrupts(uint8_t mask) { RTC->CRH=mask & 7U; }
uint8_t ABoxRtcHw_ReadCalibration(void) { return (uint8_t)(BKP->RTCCR & BKP_RTCCR_CAL); }
void ABoxRtcHw_WriteCalibration(uint8_t pulses)
{ BKP->RTCCR=(BKP->RTCCR & ~BKP_RTCCR_CAL)|pulses; }
uint32_t ABoxRtcHw_ReadOutputBits(void) { return BKP->RTCCR & 0x380U; }
void ABoxRtcHw_WriteOutputBits(uint32_t bits) { BKP->RTCCR=(BKP->RTCCR & ~0x380U)|(bits & 0x380U); }
void ABoxRtcHw_ResetBackup(void) { RCC->BDCR |= RCC_BDCR_BDRST; RCC->BDCR &= ~RCC_BDCR_BDRST; }
