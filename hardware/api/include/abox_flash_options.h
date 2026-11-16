#ifndef ABOX_FLASH_OPTIONS_H
#define ABOX_FLASH_OPTIONS_H

#include <stdint.h>
#include "abox_hal_status.h"

typedef struct {
    uint32_t write_protected;
    uint8_t read_protected;
    uint8_t watchdog_hardware;
    uint8_t reset_on_stop;
    uint8_t reset_on_standby;
    uint8_t boot_from_bank1;
    uint8_t data[2];
} ABoxFlashOptions;

/* write_protected bit 0..30 each protects two 2 KiB pages; bit 31 protects
 * pages 62..127. A set bit means protected. boot_from_bank1 is supported by
 * GD32 (and falls back to bank 0 if bank 1 is absent); STM32 requires zero.
 * ReadActive returns the option bytes loaded at the last reset, not necessarily
 * the values most recently programmed. */
ABoxHalStatus ABoxFlashOptions_ReadActive(ABoxFlashOptions *options);

/* Replaces the entire option-byte block, including both data bytes and all
 * write-protection bits. No reset is requested; changes take effect at the
 * device's next option-byte reload. Serialize this with all Flash operations.
 * Disabling read protection can cause hardware to erase the main Flash. An
 * error or power loss can leave a partially erased/programmed option block. */
ABoxHalStatus ABoxFlashOptions_Write(const ABoxFlashOptions *options);

#endif
