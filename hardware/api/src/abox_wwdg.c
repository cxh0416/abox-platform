#include "abox_wwdg.h"
#include "abox_wwdg_backend.h"
#include "abox_critical.h"

enum { WWDG_ENABLE=0x80U, WWDG_COUNTER=0x7fU, WWDG_EARLY=0x200U, WWDG_FLAG=1U };
static ABoxWwdgRegisters *registers;
static const ABoxWwdgEvents *handlers;

ABoxHalStatus ABoxWwdg_Start(const ABoxWwdgConfig *config,const ABoxWwdgEvents *events)
{
    ABoxWwdgRegisters *r;uint32_t key,divider;
    if (!config || config->window>127U || config->initial_counter<64U ||
        config->initial_counter>127U || config->early_interrupt>1U) return ABOX_HAL_INVALID;
    switch (config->prescaler) {
    case 1U: divider=0U;break;case 2U: divider=1U;break;
    case 4U: divider=2U;break;case 8U: divider=3U;break;
    default: return ABOX_HAL_INVALID;
    }
    key=ABoxCritical_Enter();
    if (registers) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    r=ABoxWwdgHw_Enable();
    if (r->control & WWDG_ENABLE) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    handlers=events;registers=r;
    r->status=0U;
    ABoxWwdgHw_Start(r,config->initial_counter,
                     (divider<<7U)|config->window|(config->early_interrupt ? WWDG_EARLY : 0U));
    ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxWwdg_Attach(const ABoxWwdgEvents *events)
{
    ABoxWwdgRegisters *r;uint32_t key=ABoxCritical_Enter();
    if (registers) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    r=ABoxWwdgHw_Enable();
    if (!(r->control&WWDG_ENABLE)) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    handlers=events;registers=r;ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxWwdg_Refresh(uint8_t counter)
{
    uint32_t key,current,window;
    if (counter<64U || counter>127U) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();
    if (!registers) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    current=registers->control&WWDG_COUNTER;window=registers->configuration&WWDG_COUNTER;
    if (current<64U) { ABoxCritical_Exit(key);return ABOX_HAL_TIMEOUT; }
    if (current>=window) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    registers->control=WWDG_ENABLE|counter;
    ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxWwdg_ReadCounter(uint8_t *counter)
{
    if (!registers || !counter) return ABOX_HAL_INVALID;
    *counter=(uint8_t)(registers->control&WWDG_COUNTER);return ABOX_HAL_OK;
}
void ABoxWwdg_Irq(void)
{
    const ABoxWwdgEvents *events=handlers;
    if (!registers || !(registers->configuration&WWDG_EARLY) || !(registers->status&WWDG_FLAG)) return;
    registers->status=0U;
    if (events && events->on_early_warning) events->on_early_warning(events->context);
}
ABoxHalStatus ABoxWwdg_SetDebugFreeze(uint8_t enabled)
{
    uint32_t key;
    if (enabled>1U) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();ABoxWwdgHw_SetDebugFreeze(enabled);ABoxCritical_Exit(key);
    return ABOX_HAL_OK;
}
