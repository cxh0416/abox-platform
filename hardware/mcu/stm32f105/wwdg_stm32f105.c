#include <stddef.h>
#include "abox_wwdg_backend.h"
#include "stm32f1xx_hal.h"

_Static_assert(offsetof(ABoxWwdgRegisters,configuration)==offsetof(WWDG_TypeDef,CFR) &&
               offsetof(ABoxWwdgRegisters,status)==offsetof(WWDG_TypeDef,SR),"WWDG register layout");
_Static_assert(WWDG_CR_WDGA==0x80U && WWDG_CFR_EWI==0x200U && WWDG_SR_EWIF==1U,"WWDG bits");
ABoxWwdgRegisters *ABoxWwdgHw_Enable(void)
{
    __HAL_RCC_WWDG_CLK_ENABLE();return (ABoxWwdgRegisters *)(void *)WWDG;
}
void ABoxWwdgHw_Start(ABoxWwdgRegisters *r,uint8_t counter,uint32_t configuration)
{
    /* Match STM32 HAL's enable-then-configure sequence. */
    r->control=WWDG_CR_WDGA|counter;r->configuration=configuration;
}
void ABoxWwdgHw_SetDebugFreeze(uint8_t enabled)
{
    if (enabled) __HAL_DBGMCU_FREEZE_WWDG();else __HAL_DBGMCU_UNFREEZE_WWDG();
}
