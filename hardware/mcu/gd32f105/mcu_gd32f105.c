#include "abox_mcu.h"
#include <stdint.h>
#include "gd32f10x.h"
uint32_t ABoxMcu_UidWord(unsigned index)
{
    /* GD32F10x user manual rev 2.9, section 1.5.2. */
    if (index >= 3U) return 0U;
    return *(const volatile uint32_t *)(uintptr_t)(0x1FFFF7E8U + index * 4U);
}

uint32_t ABoxMcu_ResetCauses(void)
{
    uint32_t causes = 0U;
    if (rcu_flag_get(RCU_FLAG_EPRST) != RESET) causes |= ABOX_RESET_PIN;
    if (rcu_flag_get(RCU_FLAG_PORRST) != RESET) causes |= ABOX_RESET_POWER_ON;
    if (rcu_flag_get(RCU_FLAG_FWDGTRST) != RESET) causes |= ABOX_RESET_WATCHDOG;
    if (rcu_flag_get(RCU_FLAG_WWDGTRST) != RESET) causes |= ABOX_RESET_WINDOW_WATCHDOG;
    if (rcu_flag_get(RCU_FLAG_SWRST) != RESET) causes |= ABOX_RESET_SOFTWARE;
    if (rcu_flag_get(RCU_FLAG_LPRST) != RESET) causes |= ABOX_RESET_LOW_POWER;
    return causes;
}
void ABoxMcu_ClearResetCauses(void) { rcu_all_reset_flag_clear(); }
