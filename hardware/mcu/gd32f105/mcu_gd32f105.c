#include "abox_mcu.h"
#include <stdint.h>
uint32_t ABoxMcu_UidWord(unsigned index)
{
    /* GD32F10x user manual rev 2.9, section 1.5.2. */
    if (index >= 3U) return 0U;
    return *(const volatile uint32_t *)(uintptr_t)(0x1FFFF7E8U + index * 4U);
}
