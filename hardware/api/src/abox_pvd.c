#include "abox_pvd_backend.h"
#include "abox_critical.h"

static uint8_t opened,interrupt_enabled;
static ABoxPvdEvents callbacks;
ABoxHalStatus ABoxPvd_Open(const ABoxPvdConfig *config,const ABoxPvdEvents *events)
{
    uint32_t key;
    if(!config || (unsigned)config->level>ABOX_PVD_2V9 || (unsigned)config->edge>ABOX_PVD_BOTH ||
       config->interrupt>1U || config->event>1U || (events && events->changed && !config->interrupt))return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();
    if(opened) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    ABoxPvdHw_EnableClock();
    if(ABoxPvdHw_IsEnabled()) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    callbacks=events ? *events : (ABoxPvdEvents){0};interrupt_enabled=config->interrupt;opened=1U;
    ABoxPvdHw_Configure(config);ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxPvd_Read(uint8_t *below)
{ if(!opened || !below)return ABOX_HAL_INVALID;*below=ABoxPvdHw_Read();return ABOX_HAL_OK; }
ABoxHalStatus ABoxPvd_Pending(uint8_t *pending)
{ if(!opened || !pending)return ABOX_HAL_INVALID;*pending=ABoxPvdHw_Pending();return ABOX_HAL_OK; }
ABoxHalStatus ABoxPvd_Clear(void)
{ if(!opened)return ABOX_HAL_INVALID;ABoxPvdHw_Clear();return ABOX_HAL_OK; }
void ABoxPvd_Irq(void)
{
    if(opened && interrupt_enabled && ABoxPvdHw_Pending()) {
        uint8_t below=ABoxPvdHw_Read();ABoxPvdHw_Clear();
        if(callbacks.changed)callbacks.changed(callbacks.context,below);
    }
}
ABoxHalStatus ABoxPvd_Close(void)
{
    uint32_t key=ABoxCritical_Enter();
    if(!opened) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    ABoxPvdHw_Disable();opened=0U;interrupt_enabled=0U;callbacks=(ABoxPvdEvents){0};
    ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
