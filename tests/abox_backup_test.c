#include "abox_backup.h"
#include "abox_backup_backend.h"
#include "abox_platform_port.h"
#include <assert.h>
#include <stddef.h>
static uint16_t regs[43];
static uint32_t now,protect;
static unsigned clocks,writes,ends;
static int ready=1,bound=1,broken;
static uint8_t tamper_config,tamper_flags,tamper_cleared;
static uint32_t tick(void *context) { (void)context;return now++; }
static ABoxPlatformPort port={.get_tick_ms=tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return bound ? &port : NULL; }
uint32_t ABox_PortGetTickMs(void) { return now++; }
void ABoxBackupHw_Enable(void) { ++clocks; }
uint32_t ABoxBackupHw_AccessBegin(void) { uint32_t old=protect;ABoxBackupHw_Enable();protect=1U;return old; }
int ABoxBackupHw_AccessReady(void) { return ready; }
void ABoxBackupHw_AccessEnd(uint32_t old) { protect=old;++ends; }
uint16_t ABoxBackupHw_Read(unsigned index) { return regs[index]; }
void ABoxBackupHw_Write(unsigned index,uint16_t value) { ++writes;if(!broken)regs[index]=value; }
void ABoxBackupHw_TamperConfigure(uint8_t enabled,uint8_t high,uint8_t irq)
{ tamper_config=(uint8_t)(enabled|((high ? 0U : 1U)<<1U)|(irq<<2U)); }
uint8_t ABoxBackupHw_TamperConfig(void) { return tamper_config; }
uint8_t ABoxBackupHw_TamperFlags(void) { return tamper_flags; }
void ABoxBackupHw_TamperClear(uint8_t value) { tamper_cleared=value;tamper_flags &= (uint8_t)~value; }
int main(void)
{
    uint16_t value=0xaaaaU;
    assert(ABoxBackup_Read(0U,&value)==ABOX_HAL_INVALID && value==0xaaaaU && !clocks);
    assert(ABoxBackup_Read(43U,&value)==ABOX_HAL_INVALID && ABoxBackup_Read(1U,NULL)==ABOX_HAL_INVALID);
    assert(ABoxBackup_Write(43U,1U,3U)==ABOX_HAL_INVALID && !clocks);
    bound=0;assert(ABoxBackup_Write(1U,1U,3U)==ABOX_HAL_INVALID && !clocks);bound=1;
    assert(ABoxBackup_Write(1U,0x1234U,3U)==ABOX_HAL_OK && regs[1]==0x1234U && !protect);
    assert(ABoxBackup_Read(1U,&value)==ABOX_HAL_OK && value==0x1234U);
    protect=1U;assert(ABoxBackup_Write(42U,0x5678U,3U)==ABOX_HAL_OK && protect && regs[42]==0x5678U);
    ready=0;assert(ABoxBackup_Write(2U,1U,2U)==ABOX_HAL_TIMEOUT && !regs[2] && protect);ready=1;
    broken=1;assert(ABoxBackup_Write(2U,1U,2U)==ABOX_HAL_ERROR && !regs[2] && protect);
    assert(writes==3U && ends==4U);
    {
        ABoxBackupTamperConfig c={1U,0U,1U};uint8_t flags=0U;
        assert(ABoxBackup_TamperConfigure(&c,3U)==ABOX_HAL_OK && tamper_config==7U && protect);
        c.enabled=0U;assert(ABoxBackup_TamperConfigure(&c,3U)==ABOX_HAL_INVALID);
        c.interrupt_enabled=0U;ready=0;
        assert(ABoxBackup_TamperConfigure(&c,2U)==ABOX_HAL_TIMEOUT && tamper_config==7U && protect);
        ready=1;assert(ABoxBackup_TamperConfigure(&c,3U)==ABOX_HAL_OK && tamper_config==2U);
        tamper_flags=3U;assert(ABoxBackup_TamperRead(&flags)==ABOX_HAL_OK && flags==3U);
        assert(ABoxBackup_TamperClear(1U,3U)==ABOX_HAL_OK && tamper_cleared==1U && tamper_flags==2U);
        assert(ABoxBackup_TamperClear(4U,3U)==ABOX_HAL_INVALID);
    }
    return 0;
}
