#include "common.h"
#define RCU_PMU 1U
#define rcu_periph_clock_enable(value) ((void)(value),++fake_clocks)
#define RCU_CTL fake_rcc_ctl
#define RCU_CFG0 fake_rcc_cfg
#define RCU_CTL_PLLEN 0x100U
#define RCU_SCSS_IRC8M 0U
#define RCU_SCSS_HXTAL 1U
#define RCU_SCSS_PLL 2U
#define RCU_CFG0_PLLSEL 0x10000U
#define rcu_system_clock_source_get() fake_rcc_source
