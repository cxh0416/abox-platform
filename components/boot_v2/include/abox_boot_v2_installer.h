#ifndef ABOX_BOOT_V2_INSTALLER_H
#define ABOX_BOOT_V2_INSTALLER_H

#include "abox_boot_v2_boot.h"

/* The slot stream must start at offset zero on each open. Read must return
 * exactly the requested number of bytes or fail; data may contain any byte. */
typedef struct {
    void *context;
    int (*open)(void *context, uint8_t slot);
    int (*read)(void *context, uint8_t *data, uint32_t length);
    int (*close)(void *context);
    int (*flash_begin)(void *context);
    int (*flash_erase)(void *context, uint32_t address);
    int (*flash_write)(void *context, uint32_t address, const uint8_t word[4]);
    int (*flash_read)(void *context, uint32_t address, uint8_t *data, uint32_t length);
    void (*flash_end)(void *context);
    int (*vector_valid)(void *context, const uint8_t vector[8]);
    uint32_t app_start;
    uint32_t app_end; /* Exclusive, page aligned. */
    uint32_t page_size;
    uint8_t *buffer;
    uint32_t buffer_size; /* At least 8, divisible by 4. */
} ABoxBootV2Installer;

ABoxBootV2InstallResult ABoxBootV2_Install(const ABoxBootV2Installer *port,
                                          uint8_t slot, uint32_t size, uint32_t crc32);
#endif
