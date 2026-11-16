#include "abox_flash_options_backend.h"
#include "main.h"

ABoxHalStatus ABoxFlashOptionsHw_ReadActive(ABoxFlashOptions *options)
{
    FLASH_OBProgramInitTypeDef active = {0};
    HAL_FLASHEx_OBGetConfig(&active);
    options->write_protected = ~active.WRPPage;
    options->read_protected = active.RDPLevel != OB_RDP_LEVEL_0;
    options->watchdog_hardware = (active.USERConfig & 1U) == 0U;
    options->reset_on_stop = (active.USERConfig & 2U) == 0U;
    options->reset_on_standby = (active.USERConfig & 4U) == 0U;
    options->boot_from_bank1 = 0U;
    options->data[0] = (uint8_t)HAL_FLASHEx_OBGetUserData(OB_DATA_ADDRESS_DATA0);
    options->data[1] = (uint8_t)HAL_FLASHEx_OBGetUserData(OB_DATA_ADDRESS_DATA1);
    return ABOX_HAL_OK;
}

static HAL_StatusTypeDef program_option(volatile uint16_t *address, uint8_t value)
{
    if (value == 0xffU) return HAL_OK; /* The erase value needs no programming. */
    *address = value;
    return FLASH_WaitForLastOperation(FLASH_TIMEOUT_VALUE);
}

ABoxHalStatus ABoxFlashOptionsHw_Write(const ABoxFlashOptions *options)
{
    HAL_StatusTypeDef status;
    uint32_t unprotected = ~options->write_protected;
    uint8_t user = (uint8_t)(0x88U | (options->watchdog_hardware ? 0U : 1U) |
                   (options->reset_on_stop ? 0U : 2U) | (options->reset_on_standby ? 0U : 4U));
    if (options->boot_from_bank1) return ABOX_HAL_UNSUPPORTED;
    status = HAL_FLASH_Unlock();
    if ((status != HAL_OK && status != HAL_ERROR) || (FLASH->CR & FLASH_CR_LOCK)) return ABOX_HAL_ERROR;
    status = HAL_FLASH_OB_Unlock();
    if ((status != HAL_OK && status != HAL_ERROR) || (FLASH->CR & FLASH_CR_OPTWRE) == 0U) {
        (void)HAL_FLASH_Lock();
        return ABOX_HAL_ERROR;
    }
    status = FLASH_WaitForLastOperation(FLASH_TIMEOUT_VALUE);
    if (status == HAL_OK) {
        FLASH->CR |= FLASH_CR_OPTER;
        FLASH->CR |= FLASH_CR_STRT;
        status = FLASH_WaitForLastOperation(FLASH_TIMEOUT_VALUE);
        FLASH->CR &= ~FLASH_CR_OPTER;
    }
    if (status == HAL_OK) {
        FLASH->CR |= FLASH_CR_OPTPG;
        status = program_option(&OB->RDP, options->read_protected ? OB_RDP_LEVEL_1 : OB_RDP_LEVEL_0);
        if (status == HAL_OK) status = program_option(&OB->USER, user);
        if (status == HAL_OK) status = program_option(&OB->Data0, options->data[0]);
        if (status == HAL_OK) status = program_option(&OB->Data1, options->data[1]);
        if (status == HAL_OK) status = program_option(&OB->WRP0, (uint8_t)unprotected);
        if (status == HAL_OK) status = program_option(&OB->WRP1, (uint8_t)(unprotected >> 8U));
        if (status == HAL_OK) status = program_option(&OB->WRP2, (uint8_t)(unprotected >> 16U));
        if (status == HAL_OK) status = program_option(&OB->WRP3, (uint8_t)(unprotected >> 24U));
        FLASH->CR &= ~FLASH_CR_OPTPG;
    }
    (void)HAL_FLASH_OB_Lock();
    (void)HAL_FLASH_Lock();
    return status == HAL_OK ? ABOX_HAL_OK : (status == HAL_TIMEOUT ? ABOX_HAL_TIMEOUT : ABOX_HAL_ERROR);
}
