#ifndef ABOX_BACKUP_H
#define ABOX_BACKUP_H
#include <stdint.h>
#include "abox_hal_status.h"

/* The 42 physical F105 backup registers are indexed 1..42 and hold 16 bits
 * each. The caller allocates indices and serializes access with RTC, tamper,
 * backup-domain reset and any vendor-library user. Read/Write enable APB
 * clocks, but never start or reset RTC, change its clock, or erase the domain.
 * Retention depends on the board's VBAT supply. */
ABoxHalStatus ABoxBackup_Read(unsigned index,uint16_t *value);
/* Write temporarily enables backup-domain write access and restores its
 * prior protection. An advancing platform tick is required for the timeout;
 * a failed or interrupted write must not be treated as a durable commit. */
ABoxHalStatus ABoxBackup_Write(unsigned index,uint16_t value,uint32_t timeout_ms);

#define ABOX_BACKUP_TAMPER_EVENT 1U
#define ABOX_BACKUP_TAMPER_INTERRUPT 2U
typedef struct { uint8_t enabled,active_high,interrupt_enabled; } ABoxBackupTamperConfig;
/* F105 tamper detection is destructive: an active tamper input resets all 42
 * backup data registers, potentially immediately when enabled. Caller must
 * allocate PC13, verify its electrical state, coordinate RTC output on that
 * pin, and serialize all backup-domain users. The hardware IRQ and NVIC remain
 * board-owned. Changing this configuration briefly disables detection; disabling
 * its interrupt also clears the interrupt flag in hardware. */
ABoxHalStatus ABoxBackup_TamperConfigure(const ABoxBackupTamperConfig *config,uint32_t timeout_ms);
ABoxHalStatus ABoxBackup_TamperRead(uint8_t *flags);
/* Acknowledge only the requested flags. If the tamper input remains active,
 * the event can immediately recur; backup data may remain in reset. */
ABoxHalStatus ABoxBackup_TamperClear(uint8_t flags,uint32_t timeout_ms);
#endif
