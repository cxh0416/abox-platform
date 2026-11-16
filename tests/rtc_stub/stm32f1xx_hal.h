#ifndef TEST_RTC_ST_H
#define TEST_RTC_ST_H
#include <stdint.h>
typedef struct { uint32_t CR; } PwrRegs;
typedef struct { uint32_t CR,BDCR,CSR; } RccRegs;
typedef struct { uint32_t CRH,CRL,PRLH,PRLL,CNTH,CNTL,ALRH,ALRL; } RtcRegs;
typedef struct { uint32_t RTCCR; } BkpRegs;
extern BkpRegs bkp_regs;
#define BKP (&bkp_regs)
#define BKP_RTCCR_CAL 0x7fU
extern PwrRegs pwr_regs;extern RccRegs rcc_regs;extern RtcRegs rtc_regs;
extern unsigned pwr_clock,bkp_clock;
#define PWR (&pwr_regs)
#define RCC (&rcc_regs)
#define RTC (&rtc_regs)
#define PWR_CR_DBP (1U<<8)
#define RCC_BDCR_RTCSEL (3U<<8)
#define RCC_BDCR_LSEON 1U
#define RCC_BDCR_LSEBYP 4U
#define RCC_BDCR_LSERDY 2U
#define RCC_BDCR_RTCEN (1U<<15)
#define RCC_BDCR_BDRST (1U<<16)
#define RCC_CSR_LSION 1U
#define RCC_CSR_LSIRDY 2U
#define RCC_CR_HSERDY (1U<<17)
#define RTC_CRL_SECF 1U
#define RTC_CRL_ALRF 2U
#define RTC_CRL_OWF 4U
#define RTC_CRL_RSF 8U
#define RTC_CRL_CNF 16U
#define RTC_CRL_RTOFF 32U
#define __HAL_RCC_PWR_CLK_ENABLE() (++pwr_clock)
#define __HAL_RCC_BKP_CLK_ENABLE() (++bkp_clock)
uint32_t HAL_RCC_GetPCLK1Freq(void);
#endif
