#ifndef TEST_RTC_RTC_H
#define TEST_RTC_RTC_H
#include <stdint.h>
extern uint32_t gd_rtc_inten,gd_rtc_ctl,gd_psch,gd_pscl,gd_cnth,gd_cntl,gd_alarmh,gd_alarml;
#define RTC_INTEN gd_rtc_inten
#define RTC_CTL gd_rtc_ctl
#define RTC_PSCH gd_psch
#define RTC_PSCL gd_pscl
#define RTC_CNTH gd_cnth
#define RTC_CNTL gd_cntl
#define RTC_ALRMH gd_alarmh
#define RTC_ALRML gd_alarml
#define RTC_CTL_SCIF 1U
#define RTC_CTL_ALRMIF 2U
#define RTC_CTL_OVIF 4U
#define RTC_CTL_RSYNF 8U
#define RTC_CTL_CMF 16U
#define RTC_CTL_LWOFF 32U
#endif
