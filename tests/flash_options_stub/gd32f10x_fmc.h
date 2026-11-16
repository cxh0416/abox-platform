#ifndef TEST_FLASH_OPTIONS_FMC_H
#define TEST_FLASH_OPTIONS_FMC_H
#include <stdint.h>
typedef enum { RESET, SET } FlagStatus;
typedef enum { FMC_READY, FMC_TOERR, FMC_ERR } fmc_state_enum;
extern uint32_t fmc_control;
#define FMC_CTL0 fmc_control
#define FMC_CTL0_LK 0x80U
#define FMC_CTL0_OBWEN 0x200U
#define FMC_FLAG_BANK0_BUSY 1U
#define FMC_FLAG_BANK0_PGERR 2U
#define FMC_FLAG_BANK0_WPERR 3U
#define FMC_FLAG_BANK0_END 4U
#define FMC_NSPC 0xa5U
#define FMC_USPC 0xbbU
#define OB_FWDGT_HW 0U
#define OB_FWDGT_SW 1U
#define OB_DEEPSLEEP_RST 0U
#define OB_DEEPSLEEP_NRST 2U
#define OB_STDBY_RST 0U
#define OB_STDBY_NRST 4U
#define OB_BOOT_B1 0U
#define OB_BOOT_B0 8U
FlagStatus fmc_flag_get(uint32_t flag);
void fmc_flag_clear(uint32_t flag);
void fmc_unlock(void);
void fmc_lock(void);
void ob_unlock(void);
void ob_lock(void);
fmc_state_enum ob_security_protection_config(uint8_t value);
fmc_state_enum ob_user_write(uint8_t watchdog, uint8_t stop, uint8_t standby, uint8_t boot);
fmc_state_enum ob_data_program(uint32_t address, uint8_t data);
fmc_state_enum ob_write_protection_enable(uint32_t value);
uint8_t ob_user_get(void);
uint16_t ob_data_get(void);
uint32_t ob_write_protection_get(void);
FlagStatus ob_spc_get(void);
#endif
