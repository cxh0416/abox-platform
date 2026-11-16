#ifndef ABOX_TEST_GD32_PMU_H
#define ABOX_TEST_GD32_PMU_H
#include "gd32f10x.h"
#define PMU_CTL test_pmu_words[0]
#define PMU_CS test_pmu_words[1]
#define PMU_CTL_BKPWEN (1U<<8)
#define PMU_CTL_LVDEN (1U<<4)
#define PMU_CTL_LVDT (7U<<5)
#define PMU_CS_LVDF (1U<<2)
#endif
