#ifndef TEST_WWDG_GD_DBG_H
#define TEST_WWDG_GD_DBG_H
#include <stdint.h>
extern uint32_t fake_debug;
#define DBG_CTL fake_debug
#define DBG_CTL_WWDGT_HOLD 0x200U
#endif
