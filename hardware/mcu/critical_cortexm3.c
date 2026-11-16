#include "abox_critical.h"
#if defined(ABOX_MCU_GD32F105)
#include "gd32f10x.h"
#else
#include "stm32f1xx.h"
#endif
uint32_t ABoxCritical_Enter(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    return key;
}
void ABoxCritical_Exit(uint32_t previous_mask) { __set_PRIMASK(previous_mask); }
