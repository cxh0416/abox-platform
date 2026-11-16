#include <string.h>
#include <stddef.h>
#include "abox_i2c_backend.h"
#include "abox_i2c_f105_regs.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_i2c.h"

_Static_assert(offsetof(I2C_TypeDef, OAR1) == offsetof(ABoxI2cF105Registers, own1), "I2C own address offset");
_Static_assert(offsetof(I2C_TypeDef, DR) == offsetof(ABoxI2cF105Registers, data), "I2C data offset");
_Static_assert(offsetof(I2C_TypeDef, SR2) == offsetof(ABoxI2cF105Registers, status2), "I2C status offset");

static I2C_HandleTypeDef g_i2c[2];
static I2C_HandleTypeDef *handle(unsigned id) { return &g_i2c[id - 1U]; }
uintptr_t ABoxI2cHw_Base(unsigned id) { return (uintptr_t)handle(id)->Instance; }
uint32_t ABoxI2cHw_OwnAddress(ABoxI2cAddress a)
{ return 0x4000U | (a.mode == ABOX_I2C_ADDRESS_10BIT ? 0x8000U | a.value : (uint32_t)a.value << 1U); }
uint32_t ABoxI2cHw_Pclk(void) { return HAL_RCC_GetPCLK1Freq(); }
uint32_t ABoxI2cHw_MaxPclk(void) { return 36000000U; }
ABoxHalStatus ABoxI2cHw_Open(unsigned id, uint32_t speed, ABoxI2cDuty duty)
{
    I2C_HandleTypeDef *h = handle(id);
    uint32_t pclk,enabled;
    memset(h, 0, sizeof(*h));
    h->Instance = id == 1U ? I2C1 : I2C2;
    h->Init.ClockSpeed = speed;
    h->Init.DutyCycle = duty == ABOX_I2C_DUTY_2 ? I2C_DUTYCYCLE_2 : I2C_DUTYCYCLE_16_9;
    h->Init.OwnAddress1 = 0U;
    h->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    h->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h->Init.OwnAddress2 = 0U;
    h->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    h->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (id == 1U) __HAL_RCC_I2C1_CLK_ENABLE(); else __HAL_RCC_I2C2_CLK_ENABLE();
    if (HAL_I2C_Init(h) != HAL_OK) {
        (void)HAL_I2C_DeInit(h);
        if (id == 1U) __HAL_RCC_I2C1_CLK_DISABLE(); else __HAL_RCC_I2C2_CLK_DISABLE();
        return ABOX_HAL_ERROR;
    }
    /* HAL derives the fast-mode rise time from truncated whole MHz; use the
     * actual PCLK1 when it is fractional, with the peripheral disabled. */
    pclk=HAL_RCC_GetPCLK1Freq();enabled=h->Instance->CR1 & I2C_CR1_PE;
    h->Instance->CR1 &= ~I2C_CR1_PE;
    h->Instance->TRISE=speed<=100000U ? pclk/1000000U+1U : (uint32_t)(((uint64_t)pclk*3U)/10000000U)+1U;
    h->Instance->CR1 |= enabled;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2cHw_Close(unsigned id)
{
    if (HAL_I2C_DeInit(handle(id)) != HAL_OK) return ABOX_HAL_ERROR;
    if (id == 1U) __HAL_RCC_I2C1_CLK_DISABLE(); else __HAL_RCC_I2C2_CLK_DISABLE();
    return ABOX_HAL_OK;
}
int ABoxI2cHw_Flag(unsigned id, ABoxI2cHwFlag flag)
{
    I2C_HandleTypeDef *h = handle(id);
    switch (flag) {
    case ABOX_I2C_HW_BUSY: return __HAL_I2C_GET_FLAG(h, I2C_FLAG_BUSY) == SET;
    case ABOX_I2C_HW_START: return __HAL_I2C_GET_FLAG(h, I2C_FLAG_SB) == SET;
    case ABOX_I2C_HW_ADDRESS: return __HAL_I2C_GET_FLAG(h, I2C_FLAG_ADDR) == SET;
    case ABOX_I2C_HW_HEADER10: return __HAL_I2C_GET_FLAG(h, I2C_FLAG_ADD10) == SET;
    case ABOX_I2C_HW_TX_EMPTY: return __HAL_I2C_GET_FLAG(h, I2C_FLAG_TXE) == SET;
    case ABOX_I2C_HW_FINISHED: return __HAL_I2C_GET_FLAG(h, I2C_FLAG_BTF) == SET;
    case ABOX_I2C_HW_RX_READY: return __HAL_I2C_GET_FLAG(h, I2C_FLAG_RXNE) == SET;
    case ABOX_I2C_HW_STOP_PENDING: return (h->Instance->CR1 & I2C_CR1_STOP) != 0U;
    default: return 0;
    }
}
ABoxHalStatus ABoxI2cHw_Error(unsigned id)
{
    I2C_HandleTypeDef *h = handle(id);
    uint32_t errors = h->Instance->SR1 & (I2C_SR1_ARLO | I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_OVR | I2C_SR1_TIMEOUT);
    if (!errors) return ABOX_HAL_OK;
    __HAL_I2C_CLEAR_FLAG(h, errors);
    if (errors & I2C_SR1_ARLO) return ABOX_HAL_ARBITRATION_LOST;
    if (errors & I2C_SR1_TIMEOUT) return ABOX_HAL_BUS_TIMEOUT;
    if (errors & (I2C_SR1_BERR | I2C_SR1_OVR)) return ABOX_HAL_ERROR;
    return ABOX_HAL_NO_ACK;
}
void ABoxI2cHw_Start(unsigned id) { SET_BIT(handle(id)->Instance->CR1, I2C_CR1_START); }
void ABoxI2cHw_Stop(unsigned id) { SET_BIT(handle(id)->Instance->CR1, I2C_CR1_STOP); }
void ABoxI2cHw_Address(unsigned id, uint8_t address, int read)
{ handle(id)->Instance->DR = ((uint32_t)address << 1U) | (read ? 1U : 0U); }
void ABoxI2cHw_AddressByte(unsigned id, uint8_t value) { handle(id)->Instance->DR = value; }
void ABoxI2cHw_ClearAddress(unsigned id) { __HAL_I2C_CLEAR_ADDRFLAG(handle(id)); }
void ABoxI2cHw_Ack(unsigned id, int enabled)
{ if (enabled) SET_BIT(handle(id)->Instance->CR1, I2C_CR1_ACK); else CLEAR_BIT(handle(id)->Instance->CR1, I2C_CR1_ACK); }
void ABoxI2cHw_AckNext(unsigned id, int next)
{ if (next) SET_BIT(handle(id)->Instance->CR1, I2C_CR1_POS); else CLEAR_BIT(handle(id)->Instance->CR1, I2C_CR1_POS); }
void ABoxI2cHw_Write(unsigned id, uint8_t value) { handle(id)->Instance->DR = value; }
uint8_t ABoxI2cHw_Read(unsigned id) { return (uint8_t)handle(id)->Instance->DR; }
uint32_t ABoxI2cHw_EnterCritical(void) { uint32_t key = __get_PRIMASK(); __disable_irq(); return key; }
void ABoxI2cHw_LeaveCritical(uint32_t key) { __set_PRIMASK(key); }

uintptr_t ABoxI2cHw_DmaAddress(unsigned id) { return (uintptr_t)&handle(id)->Instance->DR; }
void ABoxI2cHw_DmaRequest(unsigned id,int enabled,int last)
{
    I2C_TypeDef *p=handle(id)->Instance;
    if (enabled) { if (last) p->CR2 |= I2C_CR2_LAST; else p->CR2 &= ~I2C_CR2_LAST; p->CR2 |= I2C_CR2_DMAEN; }
    else p->CR2 &= ~(I2C_CR2_DMAEN|I2C_CR2_LAST);
}
