#include "abox_gd32f105_flash.h"

#include <string.h>
#include "gd32f10x_fmc.h"

#define ABOX_GD_FLASH_START 0x08000000U
#define ABOX_GD_FLASH_END   0x08040000U
#define ABOX_GD_FLASH_PAGE  2048U

int ABoxGd32Flash_Begin(void *context)
{
    (void)context;
    if (fmc_flag_get(FMC_FLAG_BANK0_BUSY) != RESET) return 0;
    fmc_unlock();
    if ((FMC_CTL0 & FMC_CTL0_LK) != 0U) {
        fmc_lock();
        return 0;
    }
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    return 1;
}

int ABoxGd32Flash_Write(void *context, uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t word;
    uint16_t halfword;
    (void)context;
    if (!data || (length != sizeof(word) && length != sizeof(halfword)) || (address & (length - 1U)) ||
        address < ABOX_GD_FLASH_START || address > ABOX_GD_FLASH_END - length ||
        (FMC_CTL0 & FMC_CTL0_LK) != 0U) return 0;
    if (length == sizeof(halfword)) {
        memcpy(&halfword, data, sizeof(halfword));
        return fmc_halfword_program(address, halfword) == FMC_READY;
    }
    memcpy(&word, data, sizeof(word));
    return fmc_word_program(address, word) == FMC_READY;
}

int ABoxGd32Flash_ErasePage(void *context, uint32_t address)
{
    (void)context;
    if ((address & (ABOX_GD_FLASH_PAGE - 1U)) || address < ABOX_GD_FLASH_START ||
        address > ABOX_GD_FLASH_END - ABOX_GD_FLASH_PAGE ||
        (FMC_CTL0 & FMC_CTL0_LK) != 0U) return 0;
    return fmc_page_erase(address) == FMC_READY;
}

void ABoxGd32Flash_End(void *context)
{
    (void)context;
    fmc_lock();
}
