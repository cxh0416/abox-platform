#include <assert.h>
#include <stdint.h>
#include "abox_wwdg_backend.h"
static unsigned clock_enabled;
uint32_t fake_debug;
#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
WWDG_TypeDef fake_st_wwdg;
void fake_clock_enable(void) { ++clock_enabled; }
#else
#include "gd32f10x.h"
#include "gd32f10x_rcu.h"
void rcu_periph_clock_enable(uint32_t peripheral) { assert(peripheral==RCU_WWDGT);++clock_enabled; }
#endif
int main(void)
{
    ABoxWwdgRegisters r={0U,0U,0U};
    ABoxWwdgRegisters *hw=ABoxWwdgHw_Enable();
#ifdef TEST_STM32
    assert(hw==(ABoxWwdgRegisters *)(void *)&fake_st_wwdg);
#else
    assert((uintptr_t)hw==WWDGT_BASE);
#endif
    assert(clock_enabled==1U);
    ABoxWwdgHw_Start(&r,127U,0x3d0U);
    assert(r.control==0xffU && r.configuration==0x3d0U);
    fake_debug=0x400U;ABoxWwdgHw_SetDebugFreeze(1U);assert(fake_debug==0x600U);
    ABoxWwdgHw_SetDebugFreeze(0U);assert(fake_debug==0x400U);
    return 0;
}
