#include "abox_platform_config.h"

int main(void)
{
    ABoxProductConfig config = {
        "test", 0x08000000U, 256U * 1024U, 0x08000000U, 0x8000U,
        0x08008000U, 0x0803F800U, 0x800U, ABOX_SCHEDULER_BAREMETAL
    };
    if (!ABox_ProductConfigIsValid(&config)) return 1;
    config.flash_base = config.boot_start = 0xFF000000U;
    config.flash_size = 0x02000000U;
    config.app_start = 0xFF008000U;
    config.ota_info_start = 0xFF03F800U;
    if (ABox_ProductConfigIsValid(&config)) return 1;
    return 0;
}
