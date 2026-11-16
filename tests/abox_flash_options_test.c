#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_flash_options.h"

static unsigned calls, fail_at;

#if TEST_STM32
#include "main.h"
FLASH_TypeDef flash_registers;
OB_TypeDef option_registers;
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { FLASH->CR &= ~FLASH_CR_LOCK; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { FLASH->CR |= FLASH_CR_LOCK; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_OB_Unlock(void) { FLASH->CR |= FLASH_CR_OPTWRE; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_OB_Lock(void) { FLASH->CR &= ~FLASH_CR_OPTWRE; return HAL_OK; }
HAL_StatusTypeDef FLASH_WaitForLastOperation(uint32_t timeout)
{
    (void)timeout;
    ++calls;
    if (calls == fail_at) return HAL_TIMEOUT;
    if (calls == 2U) memset(&option_registers, 0xff, sizeof option_registers);
    return HAL_OK;
}
void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *active)
{ active->WRPPage = 0xffff00ffU; active->RDPLevel = OB_RDP_LEVEL_1; active->USERConfig = 5U; }
uint32_t HAL_FLASHEx_OBGetUserData(uint32_t address)
{ return address == OB_DATA_ADDRESS_DATA0 ? 0x12U : 0x34U; }
#else
#include "gd32f10x_fmc.h"
uint32_t fmc_control;
static uint32_t programmed_wp, programmed_data[2];
static uint8_t programmed_spc, programmed_user;
static fmc_state_enum operation(void) { return ++calls == fail_at ? FMC_TOERR : FMC_READY; }
FlagStatus fmc_flag_get(uint32_t flag) { (void)flag; return RESET; }
void fmc_flag_clear(uint32_t flag) { (void)flag; }
void fmc_unlock(void) { FMC_CTL0 &= ~FMC_CTL0_LK; }
void fmc_lock(void) { FMC_CTL0 |= FMC_CTL0_LK; }
void ob_unlock(void) { FMC_CTL0 |= FMC_CTL0_OBWEN; }
void ob_lock(void) { FMC_CTL0 &= ~FMC_CTL0_OBWEN; }
fmc_state_enum ob_security_protection_config(uint8_t value) { programmed_spc = value; return operation(); }
fmc_state_enum ob_user_write(uint8_t w, uint8_t s, uint8_t t, uint8_t b)
{ programmed_user = w | s | t | b; return operation(); }
fmc_state_enum ob_data_program(uint32_t address, uint8_t data)
{ programmed_data[address == 0x1ffff804U ? 0 : 1] = data; return operation(); }
fmc_state_enum ob_write_protection_enable(uint32_t value) { programmed_wp = value; return operation(); }
uint8_t ob_user_get(void) { return 13U; }
uint16_t ob_data_get(void) { return 0x3412U; }
uint32_t ob_write_protection_get(void) { return 0xffff00ffU; }
FlagStatus ob_spc_get(void) { return SET; }
#endif

int main(void)
{
    ABoxFlashOptions active, desired = {0};
    assert(ABoxFlashOptions_ReadActive(0) == ABOX_HAL_INVALID);
    assert(ABoxFlashOptions_Write(0) == ABOX_HAL_INVALID);
    assert(ABoxFlashOptions_ReadActive(&active) == ABOX_HAL_OK);
    assert(active.write_protected == 0x0000ff00U && active.read_protected == 1U);
    assert(active.watchdog_hardware == 0U && active.reset_on_stop == 1U && active.reset_on_standby == 0U);
    assert(active.data[0] == 0x12U && active.data[1] == 0x34U && active.boot_from_bank1 == 0U);
    desired.write_protected = 0x12345678U;
    desired.read_protected = 1U;
    desired.watchdog_hardware = 1U;
    desired.reset_on_stop = 1U;
    desired.data[0] = 0x25U;
    desired.data[1] = 0x53U;
    assert(ABoxFlashOptions_Write(&desired) == ABOX_HAL_OK);
#if TEST_STM32
    assert(option_registers.RDP == OB_RDP_LEVEL_1 && option_registers.USER == 0x8cU);
    assert(option_registers.Data0 == 0x25U && option_registers.Data1 == 0x53U);
    assert(option_registers.WRP0 == 0x87U && option_registers.WRP1 == 0xa9U &&
           option_registers.WRP2 == 0xcbU && option_registers.WRP3 == 0xedU);
    assert((FLASH->CR & (FLASH_CR_LOCK | FLASH_CR_OPTWRE)) == FLASH_CR_LOCK);
    desired.boot_from_bank1 = 1U;
    assert(ABoxFlashOptions_Write(&desired) == ABOX_HAL_UNSUPPORTED);
    desired.boot_from_bank1 = 0U;
#else
    assert(programmed_spc == FMC_USPC && programmed_user == 12U);
    assert(programmed_data[0] == 0x25U && programmed_data[1] == 0x53U && programmed_wp == desired.write_protected);
    assert((FMC_CTL0 & (FMC_CTL0_LK | FMC_CTL0_OBWEN)) == FMC_CTL0_LK);
    desired.boot_from_bank1 = 1U;
    assert(ABoxFlashOptions_Write(&desired) == ABOX_HAL_OK && programmed_user == 4U);
#endif
    desired.read_protected = 2U;
    assert(ABoxFlashOptions_Write(&desired) == ABOX_HAL_INVALID);
    desired.read_protected = 0U;
    calls = 0U; fail_at = 3U;
    assert(ABoxFlashOptions_Write(&desired) == ABOX_HAL_TIMEOUT);
#if TEST_STM32
    assert((FLASH->CR & (FLASH_CR_LOCK | FLASH_CR_OPTWRE)) == FLASH_CR_LOCK);
#else
    assert((FMC_CTL0 & (FMC_CTL0_LK | FMC_CTL0_OBWEN)) == FMC_CTL0_LK);
#endif
    return 0;
}
