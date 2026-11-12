#include <assert.h>
#include "main.h"
#include "abox_platform_port.h"
#include "ec_power.h"

TestFlash test_flash;
UART_HandleTypeDef huart1;
static HAL_StatusTypeDef unlock_status;
static uint32_t post_unlock_cr;
static unsigned unlock_calls;
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { unlock_calls++; test_flash.CR=post_unlock_cr; return unlock_status; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t a, uint32_t b, uint64_t c) { (void)a;(void)b;(void)c;return HAL_OK; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *a, uint32_t *b) { (void)a;(void)b;return HAL_OK; }
uint32_t HAL_GetTick(void) { return 0; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *a,uint8_t *b,uint16_t c,uint32_t d) { (void)a;(void)b;(void)c;(void)d;return HAL_OK; }
void HAL_GPIO_WritePin(void *a,uint16_t b,unsigned c) { (void)a;(void)b;(void)c; }
int ABox_PlatformPortBind(const ABoxPlatformPort *p) { return p!=0; }
uint8_t ABox_EcPower_SetConfig(const ABoxEcPowerConfig *p) { return p!=0; }
#include "../targets/stm32f105_ec800_v1/platform_port_stm32f105.c"
static void check(HAL_StatusTypeDef status,uint32_t cr,int expected) { unlock_status=status; post_unlock_cr=cr; unlock_calls=0; assert(flash_begin(0)==expected); assert(unlock_calls==1); }
int main(void) {
    check(HAL_OK,0,1);
    check(HAL_ERROR,0,1); /* observed on the bench */
    check(HAL_ERROR,FLASH_CR_LOCK,0);
    check(HAL_OK,FLASH_CR_LOCK,0);
    check(HAL_BUSY,0,0);
    check(HAL_TIMEOUT,0,0);
    return 0;
}
