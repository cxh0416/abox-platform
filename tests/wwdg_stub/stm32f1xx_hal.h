#ifndef TEST_WWDG_ST_H
#define TEST_WWDG_ST_H
#include <stdint.h>
typedef struct { volatile uint32_t CR,CFR,SR; } WWDG_TypeDef;
extern WWDG_TypeDef fake_st_wwdg;
extern uint32_t fake_debug;
void fake_clock_enable(void);
#define WWDG (&fake_st_wwdg)
#define WWDG_CR_WDGA 0x80U
#define WWDG_CFR_EWI 0x200U
#define WWDG_SR_EWIF 1U
#define __HAL_RCC_WWDG_CLK_ENABLE() fake_clock_enable()
#define __HAL_DBGMCU_FREEZE_WWDG() (fake_debug|=0x200U)
#define __HAL_DBGMCU_UNFREEZE_WWDG() (fake_debug&=~0x200U)
#endif
