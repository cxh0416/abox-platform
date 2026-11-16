#ifndef TEST_MCU_GD32_H
#define TEST_MCU_GD32_H
#include <stdint.h>
#define RCU_FLAG_EPRST 0U
#define RCU_FLAG_PORRST 1U
#define RCU_FLAG_FWDGTRST 2U
#define RCU_FLAG_WWDGTRST 3U
#define RCU_FLAG_SWRST 4U
#define RCU_FLAG_LPRST 5U
#define RESET 0U
uint32_t rcu_flag_get(uint32_t flag);
void rcu_all_reset_flag_clear(void);
#endif
