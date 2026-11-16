#ifndef TEST_WWDG_GD_RCU_H
#define TEST_WWDG_GD_RCU_H
#include <stdint.h>
#define RCU_WWDGT 11U
void rcu_periph_clock_enable(uint32_t peripheral);
#endif
