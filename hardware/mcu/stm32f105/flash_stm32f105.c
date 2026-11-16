#include <string.h>
#include "abox_flash_backend.h"
#include "main.h"

static const ABoxFlashGeometry geometry = {0x08000000U, 0x40000U, 2048U, 2U, 4U};
const ABoxFlashGeometry *ABoxFlashHw_Geometry(void) { return &geometry; }
int ABoxFlashHw_Begin(void)
{
    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    /* The controller lock bit is authoritative if HAL reported ERROR after
     * an effective unlock; never proceed on HAL_BUSY or while still locked. */
    return (status == HAL_OK || status == HAL_ERROR) && (FLASH->CR & FLASH_CR_LOCK) == 0U;
}
int ABoxFlashHw_Program(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t word;
    uint16_t halfword;
    if (!data || (length != sizeof(word) && length != sizeof(halfword)) ||
        (address & (length - 1U)) || (FLASH->CR & FLASH_CR_LOCK) != 0U) return 0;
    if (length == sizeof(halfword)) {
        memcpy(&halfword, data, sizeof(halfword));
        return HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, halfword) == HAL_OK;
    }
    memcpy(&word, data, sizeof(word));
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word) == HAL_OK;
}
int ABoxFlashHw_ErasePage(uint32_t address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t error = 0U;
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U) return 0;
    erase.TypeErase = FLASH_TYPEERASE_PAGES; erase.PageAddress = address; erase.NbPages = 1U;
    return HAL_FLASHEx_Erase(&erase, &error) == HAL_OK;
}
int ABoxFlashHw_End(void) { return HAL_FLASH_Lock() == HAL_OK; }
int ABoxFlashHw_Read(uint32_t address, uint8_t *data, uint32_t length)
{ memcpy(data, (const void *)(uintptr_t)address, length); return 1; }
