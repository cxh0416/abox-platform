#include "abox_flash_backend.h"
static uint8_t g_active;

static int valid_range(const ABoxFlashGeometry *g, uint32_t address, uint32_t length)
{
    uint64_t end = (uint64_t)address + length;
    return g && g->size && length && address >= g->base &&
           end <= (uint64_t)g->base + g->size && end <= 0x100000000ULL;
}
ABoxHalStatus ABoxFlash_Geometry(ABoxFlashGeometry *geometry)
{
    const ABoxFlashGeometry *g = ABoxFlashHw_Geometry();
    if (!geometry || !g) return ABOX_HAL_INVALID;
    *geometry = *g;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxFlash_Begin(void)
{
    if (g_active) return ABOX_HAL_BUSY;
    if (!ABoxFlashHw_Begin()) return ABOX_HAL_ERROR;
    g_active = 1U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxFlash_Program(uint32_t address, const uint8_t *data, uint32_t length)
{
    const ABoxFlashGeometry *g = ABoxFlashHw_Geometry();
    uint32_t offset = 0U;
    if (!g_active) return ABOX_HAL_INVALID;
    if (!data || !g || !g->program_unit || !g->program_max_size ||
        (g->program_unit & (g->program_unit - 1U)) ||
        (g->program_max_size & (g->program_max_size - 1U)) || g->program_max_size < g->program_unit ||
        !valid_range(g, address, length) || (address % g->program_unit) || (length % g->program_unit))
        return ABOX_HAL_INVALID;
    while (offset < length) {
        uint32_t chunk = g->program_max_size;
        while (chunk > g->program_unit && ((address + offset) % chunk || length - offset < chunk)) chunk /= 2U;
        if (!ABoxFlashHw_Program(address + offset, data + offset, chunk)) return ABOX_HAL_ERROR;
        offset += chunk;
    }
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxFlash_ErasePage(uint32_t address)
{
    const ABoxFlashGeometry *g = ABoxFlashHw_Geometry();
    if (!g_active || !g || !g->erase_size || address < g->base ||
        (address - g->base) % g->erase_size || !valid_range(g, address, g->erase_size)) return ABOX_HAL_INVALID;
    return ABoxFlashHw_ErasePage(address) ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
ABoxHalStatus ABoxFlash_End(void)
{
    if (!g_active) return ABOX_HAL_INVALID;
    if (!ABoxFlashHw_End()) return ABOX_HAL_ERROR;
    g_active = 0U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxFlash_Read(uint32_t address, uint8_t *data, uint32_t length)
{
    const ABoxFlashGeometry *g = ABoxFlashHw_Geometry();
    if (!data || !valid_range(g, address, length)) return ABOX_HAL_INVALID;
    return ABoxFlashHw_Read(address, data, length) ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
