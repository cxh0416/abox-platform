#include "abox_watchdog_port.h"
#include "iwdg.h"

static uint8_t ready;

int ABoxWatchdog_Init(uint16_t prescaler, uint16_t reload)
{
    uint32_t value;
    if (ready || reload > 4095U) return 0;
    switch (prescaler) {
    case 4U: value = IWDG_PRESCALER_4; break;
    case 8U: value = IWDG_PRESCALER_8; break;
    case 16U: value = IWDG_PRESCALER_16; break;
    case 32U: value = IWDG_PRESCALER_32; break;
    case 64U: value = IWDG_PRESCALER_64; break;
    case 128U: value = IWDG_PRESCALER_128; break;
    case 256U: value = IWDG_PRESCALER_256; break;
    default: return 0;
    }
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = value;
    hiwdg.Init.Reload = reload;
    ready = HAL_IWDG_Init(&hiwdg) == HAL_OK;
    return ready;
}
int ABoxWatchdog_Refresh(void) { return ready && HAL_IWDG_Refresh(&hiwdg) == HAL_OK; }
void ABoxWatchdog_FreezeWhileDebugging(void) { __HAL_DBGMCU_FREEZE_IWDG(); }
