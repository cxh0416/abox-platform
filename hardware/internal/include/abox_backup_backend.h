#ifndef ABOX_BACKUP_BACKEND_H
#define ABOX_BACKUP_BACKEND_H
#include <stdint.h>
void ABoxBackupHw_Enable(void);
uint32_t ABoxBackupHw_AccessBegin(void);
int ABoxBackupHw_AccessReady(void);
void ABoxBackupHw_AccessEnd(uint32_t previous);
uint16_t ABoxBackupHw_Read(unsigned index);
void ABoxBackupHw_Write(unsigned index,uint16_t value);
void ABoxBackupHw_TamperConfigure(uint8_t enabled,uint8_t active_high,uint8_t irq);
uint8_t ABoxBackupHw_TamperConfig(void);
uint8_t ABoxBackupHw_TamperFlags(void);
void ABoxBackupHw_TamperClear(uint8_t flags);
#endif
