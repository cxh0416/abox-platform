#include "abox_mcu.h"
#include "main.h"
uint32_t ABoxMcu_UidWord(unsigned index)
{
    switch (index) {
    case 0U: return HAL_GetUIDw0();
    case 1U: return HAL_GetUIDw1();
    case 2U: return HAL_GetUIDw2();
    default: return 0U;
    }
}

uint32_t ABoxMcu_ResetCauses(void)
{
    uint32_t causes = 0U;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) causes |= ABOX_RESET_PIN;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) causes |= ABOX_RESET_POWER_ON;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) causes |= ABOX_RESET_WATCHDOG;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) causes |= ABOX_RESET_WINDOW_WATCHDOG;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) causes |= ABOX_RESET_SOFTWARE;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) causes |= ABOX_RESET_LOW_POWER;
    return causes;
}
void ABoxMcu_ClearResetCauses(void) { __HAL_RCC_CLEAR_RESET_FLAGS(); }
