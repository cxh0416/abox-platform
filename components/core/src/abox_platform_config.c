#include "abox_platform_config.h"
#include "abox_platform_port.h"

int ABox_ProductConfigIsValid(const ABoxProductConfig *config)
{
    uint64_t flash_end;
    uint64_t boot_end;
    uint64_t app_end;
    uint64_t ota_end;

    if (config == 0 || config->product_id == 0 || config->product_id[0] == '\0') return 0;
    if (config->flash_size == 0U || config->boot_size == 0U || config->ota_info_size == 0U) return 0;
    if (config->flash_base == 0U || config->boot_start != config->flash_base) return 0;
    if (config->ota_info_start < config->app_start) return 0;

    flash_end = (uint64_t)config->flash_base + config->flash_size;
    boot_end = (uint64_t)config->boot_start + config->boot_size;
    app_end = (uint64_t)config->ota_info_start;
    ota_end = (uint64_t)config->ota_info_start + config->ota_info_size;

    if (flash_end > 0x100000000ULL || boot_end > app_end || ota_end > flash_end) return 0;
    if (config->scheduler != ABOX_SCHEDULER_BAREMETAL &&
        config->scheduler != ABOX_SCHEDULER_FREERTOS) return 0;
    return 1;
}

int ABox_PlatformPortIsValid(const ABoxPlatformPort *port)
{
    if (port == 0 || port->get_tick_ms == 0 || port->uart_write == 0) return 0;
    if (port->flash_begin == 0 || port->flash_write == 0 || port->flash_erase_page == 0 ||
        port->flash_end == 0 || port->flash_layout == 0) return 0;
    if (port->enter_critical == 0 || port->exit_critical == 0) return 0;
    if (port->flash_layout->app_start_addr == 0U ||
        port->flash_layout->ota_info_addr <= port->flash_layout->app_start_addr ||
        port->flash_layout->flash_page_size == 0U) return 0;
    return 1;
}
