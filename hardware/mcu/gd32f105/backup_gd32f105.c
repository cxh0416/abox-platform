#include "abox_backup_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_pmu.h"
static uintptr_t reg(unsigned index)
{ return (uintptr_t)BKP_BASE+(index<=10U ? index*4U : 0x40U+(index-11U)*4U); }
void ABoxBackupHw_Enable(void) { rcu_periph_clock_enable(RCU_PMU);rcu_periph_clock_enable(RCU_BKPI); }
uint32_t ABoxBackupHw_AccessBegin(void)
{ uint32_t old;ABoxBackupHw_Enable();old=PMU_CTL&PMU_CTL_BKPWEN;PMU_CTL|=PMU_CTL_BKPWEN;return old; }
int ABoxBackupHw_AccessReady(void) { return (PMU_CTL&PMU_CTL_BKPWEN)!=0U; }
void ABoxBackupHw_AccessEnd(uint32_t previous) { if(!previous)PMU_CTL&=~PMU_CTL_BKPWEN; }
uint16_t ABoxBackupHw_Read(unsigned index) { return (uint16_t)REG32(reg(index)); }
void ABoxBackupHw_Write(unsigned index,uint16_t value) { REG32(reg(index))=value; }
/* BKP_TPCTL/BKP_TPCS offsets and write-one-to-clear bits in the GD manual. */
#define TAMPER_CONTROL REG32(BKP_BASE+0x30U)
#define TAMPER_STATUS REG32(BKP_BASE+0x34U)
void ABoxBackupHw_TamperConfigure(uint8_t enabled,uint8_t high,uint8_t irq)
{
    TAMPER_CONTROL &= ~1U;
    TAMPER_CONTROL = high ? 0U : 2U;
    TAMPER_STATUS = irq ? 4U : 0U;
    if (enabled) TAMPER_CONTROL |= 1U;
}
uint8_t ABoxBackupHw_TamperConfig(void)
{ return (uint8_t)((TAMPER_CONTROL & 3U)|(TAMPER_STATUS & 4U)); }
uint8_t ABoxBackupHw_TamperFlags(void) { return (uint8_t)((TAMPER_STATUS>>8U)&3U); }
void ABoxBackupHw_TamperClear(uint8_t flags) { TAMPER_STATUS=(TAMPER_STATUS & 4U)|(flags & 3U); }
