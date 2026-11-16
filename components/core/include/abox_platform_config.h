#ifndef ABOX_PLATFORM_CONFIG_H
#define ABOX_PLATFORM_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ABOX_SCHEDULER_BAREMETAL = 0,
    ABOX_SCHEDULER_FREERTOS = 1
} ABoxSchedulerKind;

typedef struct {
    const char *product_id;
    uint32_t flash_base;
    uint32_t flash_size;
    uint32_t boot_start;
    uint32_t boot_size;
    uint32_t app_start;
    uint32_t ota_info_start;
    uint32_t ota_info_size;
    ABoxSchedulerKind scheduler;
} ABoxProductConfig;

/* Returns zero for a malformed or overlapping Flash/product configuration. */
int ABox_ProductConfigIsValid(const ABoxProductConfig *config);

#ifdef __cplusplus
}
#endif

#endif
