#include "abox_flash_options_backend.h"
#include "gd32f10x_fmc.h"

ABoxHalStatus ABoxFlashOptionsHw_ReadActive(ABoxFlashOptions *options)
{
    uint8_t user = ob_user_get();
    uint16_t data = ob_data_get();
    options->write_protected = ~ob_write_protection_get();
    options->read_protected = ob_spc_get() != RESET;
    options->watchdog_hardware = (user & 1U) == 0U;
    options->reset_on_stop = (user & 2U) == 0U;
    options->reset_on_standby = (user & 4U) == 0U;
    options->boot_from_bank1 = (user & 8U) == 0U;
    options->data[0] = (uint8_t)data;
    options->data[1] = (uint8_t)(data >> 8U);
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxFlashOptionsHw_Write(const ABoxFlashOptions *options)
{
    fmc_state_enum status;
    if (fmc_flag_get(FMC_FLAG_BANK0_BUSY) != RESET) return ABOX_HAL_BUSY;
    fmc_unlock();
    if (FMC_CTL0 & FMC_CTL0_LK) return ABOX_HAL_ERROR;
    ob_unlock();
    if ((FMC_CTL0 & FMC_CTL0_OBWEN) == 0U) { fmc_lock(); return ABOX_HAL_ERROR; }
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    /* This call erases once and writes SPC; the others only program bytes. */
    status = ob_security_protection_config(options->read_protected ? FMC_USPC : FMC_NSPC);
    if (status == FMC_READY) status = ob_user_write(options->watchdog_hardware ? OB_FWDGT_HW : OB_FWDGT_SW,
        options->reset_on_stop ? OB_DEEPSLEEP_RST : OB_DEEPSLEEP_NRST,
        options->reset_on_standby ? OB_STDBY_RST : OB_STDBY_NRST,
        options->boot_from_bank1 ? OB_BOOT_B1 : OB_BOOT_B0);
    if (status == FMC_READY && options->data[0] != 0xffU) status = ob_data_program(0x1ffff804U, options->data[0]);
    if (status == FMC_READY && options->data[1] != 0xffU) status = ob_data_program(0x1ffff806U, options->data[1]);
    if (status == FMC_READY) status = ob_write_protection_enable(options->write_protected);
    ob_lock();
    fmc_lock();
    return status == FMC_READY ? ABOX_HAL_OK : (status == FMC_TOERR ? ABOX_HAL_TIMEOUT : ABOX_HAL_ERROR);
}
