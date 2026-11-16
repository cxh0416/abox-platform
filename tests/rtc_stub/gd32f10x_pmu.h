#ifndef TEST_RTC_PMU_H
#define TEST_RTC_PMU_H
#include <stdint.h>
extern uint32_t gd_pmu;
#define PMU_CTL gd_pmu
#define PMU_CTL_BKPWEN (1U<<8)
#endif
