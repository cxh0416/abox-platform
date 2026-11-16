#include "abox_backup_backend.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>
_Static_assert(offsetof(BKP_TypeDef,DR1)==4U && offsetof(BKP_TypeDef,DR10)==0x28U &&
               offsetof(BKP_TypeDef,DR11)==0x40U && offsetof(BKP_TypeDef,DR42)==0xbcU,"STM32 backup layout");
static volatile uint32_t *reg(unsigned index)
{ return (volatile uint32_t *)((uintptr_t)BKP_BASE+(index<=10U ? index*4U : 0x40U+(index-11U)*4U)); }
void ABoxBackupHw_Enable(void) { __HAL_RCC_PWR_CLK_ENABLE();__HAL_RCC_BKP_CLK_ENABLE(); }
uint32_t ABoxBackupHw_AccessBegin(void)
{ uint32_t old;ABoxBackupHw_Enable();old=PWR->CR&PWR_CR_DBP;PWR->CR|=PWR_CR_DBP;return old; }
int ABoxBackupHw_AccessReady(void) { return (PWR->CR&PWR_CR_DBP)!=0U; }
void ABoxBackupHw_AccessEnd(uint32_t previous) { if(!previous)PWR->CR&=~PWR_CR_DBP; }
uint16_t ABoxBackupHw_Read(unsigned index) { return (uint16_t)*reg(index); }
void ABoxBackupHw_Write(unsigned index,uint16_t value) { *reg(index)=value; }
/* F105 BKP control/status registers, with write-one-to-clear status bits. */
#define TAMPER_CONTROL (*(volatile uint32_t *)((uintptr_t)BKP_BASE+0x30U))
#define TAMPER_STATUS (*(volatile uint32_t *)((uintptr_t)BKP_BASE+0x34U))
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
