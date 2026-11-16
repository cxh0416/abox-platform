#ifndef TEST_I2C_STM_HAL_H
#define TEST_I2C_STM_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { RESET=0, SET=1 } FlagStatus;
typedef struct { uint32_t CR1,CR2,OAR1,OAR2,DR,SR1,SR2,CCR,TRISE; unsigned id; } I2C_TypeDef;
typedef struct { uint32_t ClockSpeed,DutyCycle,OwnAddress1,AddressingMode,DualAddressMode,OwnAddress2,GeneralCallMode,NoStretchMode; } I2C_InitTypeDef;
typedef struct { I2C_TypeDef *Instance; I2C_InitTypeDef Init; } I2C_HandleTypeDef;
extern I2C_TypeDef fake_i2c_st[2];
#define I2C1 (&fake_i2c_st[0])
#define I2C2 (&fake_i2c_st[1])
#define I2C_DUTYCYCLE_2 0U
#define I2C_DUTYCYCLE_16_9 1U
#define I2C_ADDRESSINGMODE_7BIT 0U
#define I2C_DUALADDRESS_DISABLE 0U
#define I2C_GENERALCALL_DISABLE 0U
#define I2C_NOSTRETCH_DISABLE 0U
#define I2C_CR2_DMAEN (1U<<11)
#define I2C_CR2_LAST (1U<<12)
#define I2C_CR1_START 1U
#define I2C_CR1_STOP 2U
#define I2C_CR1_ACK 4U
#define I2C_CR1_POS 8U
#define I2C_CR1_PE 0x8000U
#define I2C_SR1_ARLO 0x100U
#define I2C_SR1_TIMEOUT 0x4000U
#define I2C_SR1_AF 0x200U
#define I2C_SR1_BERR 0x400U
#define I2C_SR1_OVR 0x800U
#define I2C_FLAG_BUSY 0U
#define I2C_FLAG_SB 1U
#define I2C_FLAG_ADDR 2U
#define I2C_FLAG_TXE 3U
#define I2C_FLAG_BTF 4U
#define I2C_FLAG_RXNE 5U
#define I2C_FLAG_ADD10 6U
#define SET_BIT(x,b) ((x)|=(b))
#define CLEAR_BIT(x,b) ((x)&=~(b))
#define __HAL_RCC_I2C1_CLK_ENABLE() fake_i2c_clock(1,1)
#define __HAL_RCC_I2C2_CLK_ENABLE() fake_i2c_clock(2,1)
#define __HAL_RCC_I2C1_CLK_DISABLE() fake_i2c_clock(1,0)
#define __HAL_RCC_I2C2_CLK_DISABLE() fake_i2c_clock(2,0)
#define __HAL_I2C_GET_FLAG(h,f) fake_i2c_flag((h)->Instance->id,(f))
#define __HAL_I2C_CLEAR_FLAG(h,f) ((h)->Instance->SR1 &= ~(f))
#define __HAL_I2C_CLEAR_ADDRFLAG(h) fake_i2c_clear_addr((h)->Instance->id)
void fake_i2c_clock(unsigned id,unsigned enabled);
FlagStatus fake_i2c_flag(unsigned id,unsigned flag);
void fake_i2c_clear_addr(unsigned id);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t key);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h);
#endif
