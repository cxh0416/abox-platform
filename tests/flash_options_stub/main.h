#ifndef TEST_FLASH_OPTIONS_MAIN_H
#define TEST_FLASH_OPTIONS_MAIN_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t CR; } FLASH_TypeDef;
typedef struct { volatile uint16_t RDP, USER, Data0, Data1, WRP0, WRP1, WRP2, WRP3; } OB_TypeDef;
typedef struct { uint32_t WRPPage, RDPLevel, USERConfig; } FLASH_OBProgramInitTypeDef;
extern FLASH_TypeDef flash_registers;
extern OB_TypeDef option_registers;
#define FLASH (&flash_registers)
#define OB (&option_registers)
#define FLASH_CR_LOCK 0x80U
#define FLASH_CR_OPTWRE 0x200U
#define FLASH_CR_OPTER 0x20U
#define FLASH_CR_STRT 0x40U
#define FLASH_CR_OPTPG 0x10U
#define FLASH_TIMEOUT_VALUE 50000U
#define OB_RDP_LEVEL_0 0xa5U
#define OB_RDP_LEVEL_1 0U
#define OB_DATA_ADDRESS_DATA0 0x1ffff804U
#define OB_DATA_ADDRESS_DATA1 0x1ffff806U
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_OB_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_OB_Lock(void);
HAL_StatusTypeDef FLASH_WaitForLastOperation(uint32_t timeout);
void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *active);
uint32_t HAL_FLASHEx_OBGetUserData(uint32_t address);
#endif
