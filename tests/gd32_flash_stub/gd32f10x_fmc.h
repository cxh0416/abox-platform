#ifndef GD32_FLASH_TEST_STUB_H
#define GD32_FLASH_TEST_STUB_H
#include <stdint.h>
typedef enum { RESET = 0, SET = 1 } FlagStatus;
typedef enum { FMC_READY = 0, FMC_BUSY, FMC_ERROR } fmc_state_enum;
extern uint32_t test_fmc_ctl0;
#define FMC_CTL0 test_fmc_ctl0
#define FMC_CTL0_LK 0x80U
#define FMC_FLAG_BANK0_BUSY 0U
#define FMC_FLAG_BANK0_PGERR 2U
#define FMC_FLAG_BANK0_WPERR 4U
#define FMC_FLAG_BANK0_END 5U
FlagStatus fmc_flag_get(uint32_t flag);
void fmc_flag_clear(uint32_t flag);
void fmc_unlock(void);
void fmc_lock(void);
fmc_state_enum fmc_word_program(uint32_t address, uint32_t word);
fmc_state_enum fmc_halfword_program(uint32_t address, uint16_t halfword);
fmc_state_enum fmc_page_erase(uint32_t address);
#endif
