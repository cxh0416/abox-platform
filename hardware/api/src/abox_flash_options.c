#include "abox_flash_options_backend.h"

ABoxHalStatus ABoxFlashOptions_ReadActive(ABoxFlashOptions *options)
{
    if (!options) return ABOX_HAL_INVALID;
    return ABoxFlashOptionsHw_ReadActive(options);
}

ABoxHalStatus ABoxFlashOptions_Write(const ABoxFlashOptions *options)
{
    if (!options || options->read_protected > 1U || options->watchdog_hardware > 1U ||
        options->reset_on_stop > 1U || options->reset_on_standby > 1U ||
        options->boot_from_bank1 > 1U) return ABOX_HAL_INVALID;
    return ABoxFlashOptionsHw_Write(options);
}
