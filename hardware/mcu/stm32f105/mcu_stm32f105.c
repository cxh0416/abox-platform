#include "abox_mcu.h"
#include "main.h"
uint32_t ABoxMcu_UidWord(unsigned index)
{
    switch (index) {
    case 0U: return HAL_GetUIDw0();
    case 1U: return HAL_GetUIDw1();
    case 2U: return HAL_GetUIDw2();
    default: return 0U;
    }
}
