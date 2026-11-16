#include "abox_backup.h"
#include "abox_backup_backend.h"
#include "abox_platform_port.h"

static int valid(unsigned index) { return index>=1U && index<=42U; }
ABoxHalStatus ABoxBackup_Read(unsigned index,uint16_t *value)
{
    if(!valid(index) || !value)return ABOX_HAL_INVALID;
    ABoxBackupHw_Enable();*value=ABoxBackupHw_Read(index);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxBackup_Write(unsigned index,uint16_t value,uint32_t timeout_ms)
{
    const ABoxPlatformPort *port=ABox_PlatformPortGet();uint32_t start,previous;
    ABoxHalStatus status;
    if(!valid(index) || !port || !port->get_tick_ms)return ABOX_HAL_INVALID;
    previous=ABoxBackupHw_AccessBegin();start=ABox_PortGetTickMs();
    while(!ABoxBackupHw_AccessReady()) {
        if((uint32_t)(ABox_PortGetTickMs()-start)>=timeout_ms) {
            ABoxBackupHw_AccessEnd(previous);return ABOX_HAL_TIMEOUT;
        }
    }
    ABoxBackupHw_Write(index,value);
    status=ABoxBackupHw_Read(index)==value ? ABOX_HAL_OK : ABOX_HAL_ERROR;
    ABoxBackupHw_AccessEnd(previous);return status;
}
static int access_begin(uint32_t timeout_ms,uint32_t *previous)
{
    const ABoxPlatformPort *port=ABox_PlatformPortGet();uint32_t start;
    if (!port || !port->get_tick_ms) return -1;
    *previous=ABoxBackupHw_AccessBegin();start=ABox_PortGetTickMs();
    while (!ABoxBackupHw_AccessReady()) {
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout_ms) {
            ABoxBackupHw_AccessEnd(*previous);return 0;
        }
    }
    return 1;
}
ABoxHalStatus ABoxBackup_TamperConfigure(const ABoxBackupTamperConfig *config,uint32_t timeout_ms)
{
    uint32_t previous;int access;uint8_t expected;
    if (!config || config->enabled>1U || config->active_high>1U || config->interrupt_enabled>1U ||
        (!config->enabled && config->interrupt_enabled)) return ABOX_HAL_INVALID;
    access=access_begin(timeout_ms,&previous);
    if (access<=0) return access<0 ? ABOX_HAL_INVALID : ABOX_HAL_TIMEOUT;
    ABoxBackupHw_TamperConfigure(config->enabled,config->active_high,config->interrupt_enabled);
    expected=(uint8_t)(config->enabled | ((config->active_high ? 0U : 1U)<<1U) | (config->interrupt_enabled<<2U));
    access=ABoxBackupHw_TamperConfig()==expected;
    ABoxBackupHw_AccessEnd(previous);return access ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxBackup_TamperRead(uint8_t *flags)
{
    if (!flags) return ABOX_HAL_INVALID;
    ABoxBackupHw_Enable();*flags=ABoxBackupHw_TamperFlags();return ABOX_HAL_OK;
}
ABoxHalStatus ABoxBackup_TamperClear(uint8_t flags,uint32_t timeout_ms)
{
    uint32_t previous;int access;
    if (!flags || (flags & ~(ABOX_BACKUP_TAMPER_EVENT|ABOX_BACKUP_TAMPER_INTERRUPT))) return ABOX_HAL_INVALID;
    access=access_begin(timeout_ms,&previous);
    if (access<=0) return access<0 ? ABOX_HAL_INVALID : ABOX_HAL_TIMEOUT;
    ABoxBackupHw_TamperClear(flags);ABoxBackupHw_AccessEnd(previous);return ABOX_HAL_OK;
}
