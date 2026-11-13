#include <assert.h>
#include <stdint.h>
#include "gd32f10x_fmc.h"
#include "abox_gd32f105_flash.h"

uint32_t test_fmc_ctl0 = FMC_CTL0_LK;
static FlagStatus busy;
static int unlock_fails;
static unsigned unlocks, locks, clears, writes, erases;
static uint32_t last_address, last_word;
static fmc_state_enum result = FMC_READY;

FlagStatus fmc_flag_get(uint32_t flag) { assert(flag == FMC_FLAG_BANK0_BUSY); return busy; }
void fmc_flag_clear(uint32_t flag)
{ assert(flag == FMC_FLAG_BANK0_PGERR || flag == FMC_FLAG_BANK0_WPERR || flag == FMC_FLAG_BANK0_END); ++clears; }
void fmc_unlock(void) { ++unlocks; if (!unlock_fails) test_fmc_ctl0 &= ~FMC_CTL0_LK; }
void fmc_lock(void) { ++locks; test_fmc_ctl0 |= FMC_CTL0_LK; }
fmc_state_enum fmc_word_program(uint32_t address, uint32_t word)
{ ++writes; last_address = address; last_word = word; return result; }
fmc_state_enum fmc_page_erase(uint32_t address)
{ ++erases; last_address = address; return result; }

int main(void)
{
    const uint8_t bytes[] = {0x12, 0x34, 0x56, 0x78};
    busy = SET;
    assert(!ABoxGd32Flash_Begin(0) && !unlocks);
    busy = RESET; unlock_fails = 1;
    assert(!ABoxGd32Flash_Begin(0) && unlocks == 1 && locks == 1 && !clears);
    assert(!ABoxGd32Flash_Write(0, 0x08000000U, bytes, 4U));
    unlock_fails = 0;
    assert(ABoxGd32Flash_Begin(0) && clears == 3);
    assert(!ABoxGd32Flash_Write(0, 0x08000000U, 0, 4U));
    assert(!ABoxGd32Flash_Write(0, 0x08000001U, bytes, 4U));
    assert(!ABoxGd32Flash_Write(0, 0x08040000U, bytes, 4U));
    assert(!ABoxGd32Flash_Write(0, 0x08000000U, bytes, 3U));
    assert(!writes);
    assert(ABoxGd32Flash_Write(0, 0x0803FFFCU, bytes, 4U));
    assert(writes == 1 && last_address == 0x0803FFFCU && last_word == 0x78563412U);
    assert(!ABoxGd32Flash_ErasePage(0, 0x0803F801U));
    assert(!ABoxGd32Flash_ErasePage(0, 0x08040000U));
    assert(!erases);
    assert(ABoxGd32Flash_ErasePage(0, 0x0803F800U) && erases == 1);
    result = FMC_ERROR;
    assert(!ABoxGd32Flash_Write(0, 0x08000000U, bytes, 4U));
    assert(!ABoxGd32Flash_ErasePage(0, 0x08000000U));
    ABoxGd32Flash_End(0);
    assert(locks == 2 && (test_fmc_ctl0 & FMC_CTL0_LK));
    return 0;
}
