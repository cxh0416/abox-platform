#ifndef TEST_RTC_GD_H
#define TEST_RTC_GD_H
#include <stdint.h>
extern uint32_t gd_bdctl,gd_rstsck,gd_ctl;
extern uint32_t gd_bkp_octl;
#define BKP_BASE 0U
#define REG32(address) gd_bkp_octl
extern unsigned pwr_clock,bkp_clock;
#define RCU_BDCTL gd_bdctl
#define RCU_RSTSCK gd_rstsck
#define RCU_CTL gd_ctl
#define RCU_BDCTL_RTCSRC (3U<<8)
#define RCU_BDCTL_LXTALEN 1U
#define RCU_BDCTL_LXTALBPS 4U
#define RCU_BDCTL_LXTALSTB 2U
#define RCU_BDCTL_RTCEN (1U<<15)
#define RCU_BDCTL_BKPRST (1U<<16)
#define RCU_RSTSCK_IRC40KEN 1U
#define RCU_RSTSCK_IRC40KSTB 2U
#define RCU_CTL_HXTALSTB (1U<<17)
#define RCU_PMU 1U
#define RCU_BKPI 2U
#define CK_APB1 3U
void rcu_periph_clock_enable(uint32_t id);
uint32_t rcu_clock_freq_get(uint32_t id);
#endif
