#include <stddef.h>
#include <stdint.h>
#include "abox_wwdg_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_rcu.h"
#include "gd32f10x_dbg.h"

_Static_assert(WWDGT_BASE==0x40002c00U && offsetof(ABoxWwdgRegisters,configuration)==4U &&
               offsetof(ABoxWwdgRegisters,status)==8U,"WWDGT register layout");
ABoxWwdgRegisters *ABoxWwdgHw_Enable(void)
{
    rcu_periph_clock_enable(RCU_WWDGT);return (ABoxWwdgRegisters *)(uintptr_t)WWDGT_BASE;
}
void ABoxWwdgHw_Start(ABoxWwdgRegisters *r,uint8_t counter,uint32_t configuration)
{
    /* Match GD32's configure-then-enable sequence. */
    r->control=counter;r->configuration=configuration;r->control|=0x80U;
}
void ABoxWwdgHw_SetDebugFreeze(uint8_t enabled)
{
    if (enabled) DBG_CTL |= DBG_CTL_WWDGT_HOLD;else DBG_CTL &= ~DBG_CTL_WWDGT_HOLD;
}
