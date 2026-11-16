#include <assert.h>
#include "abox_mcu.h"
uint32_t test_reset_flags;
#if defined(TEST_STM32)
uint32_t HAL_GetUIDw0(void) { return 10U; }
uint32_t HAL_GetUIDw1(void) { return 11U; }
uint32_t HAL_GetUIDw2(void) { return 12U; }
#else
uint32_t rcu_flag_get(uint32_t flag) { return (test_reset_flags >> flag) & 1U; }
void rcu_all_reset_flag_clear(void) { test_reset_flags = 0U; }
#endif
int main(void)
{
    test_reset_flags = (1U << 0U) | (1U << 2U) | (1U << 4U);
    assert(ABoxMcu_ResetCauses() == (ABOX_RESET_PIN | ABOX_RESET_WATCHDOG | ABOX_RESET_SOFTWARE));
    assert(ABoxMcu_ResetCauses() == (ABOX_RESET_PIN | ABOX_RESET_WATCHDOG | ABOX_RESET_SOFTWARE));
    ABoxMcu_ClearResetCauses();
    assert(ABoxMcu_ResetCauses() == 0U);
#if defined(TEST_STM32)
    assert(ABoxMcu_UidWord(0U) == 10U && ABoxMcu_UidWord(2U) == 12U && ABoxMcu_UidWord(3U) == 0U);
#endif
    return 0;
}
