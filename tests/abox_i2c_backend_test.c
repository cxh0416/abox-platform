#include <assert.h>
#include <stdint.h>
#include "abox_i2c_backend.h"
static unsigned clocks[3], primask=1U, addr_clears, sent_address, sent_direction, speed_seen, duty_seen, sent_byte;
static unsigned flags[2][11];
static uint32_t pclk=36000000U;
void fake_i2c_clock(unsigned id,unsigned on) { clocks[id]=on; }
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask=1U; }
void __set_PRIMASK(uint32_t key) { primask=key; }
#ifdef TEST_STM32
#include "stm32f1xx_hal_i2c.h"
I2C_TypeDef fake_i2c_st[2]={{.id=1},{.id=2}};
uint32_t HAL_RCC_GetPCLK1Freq(void) { return pclk; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h) { speed_seen=h->Init.ClockSpeed;duty_seen=h->Init.DutyCycle;h->Instance->CR1 |= I2C_CR1_PE;return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h) { (void)h;return HAL_OK; }
FlagStatus fake_i2c_flag(unsigned id,unsigned flag) { return flags[id-1U][flag]?SET:RESET; }
void fake_i2c_clear_addr(unsigned id) { assert(id==1U);++addr_clears; }
static uint32_t *control(void) { return &fake_i2c_st[0].CR1; }
static void set_errors(unsigned mask) { fake_i2c_st[0].SR1=mask; }
static unsigned errors(void) { return fake_i2c_st[0].SR1; }
static void observe_address(void) { sent_address=fake_i2c_st[0].DR & 0xfeU;sent_direction=fake_i2c_st[0].DR & 1U; }
static void observe_write(void) { sent_byte=fake_i2c_st[0].DR; }
static void set_read(uint8_t data) { fake_i2c_st[0].DR=data; }
#define ERR_ARB I2C_SR1_ARLO
#define ERR_NACK I2C_SR1_AF
#define ERR_BUS I2C_SR1_BERR
#define ERR_TIMEOUT I2C_SR1_TIMEOUT
#else
#include "gd32f10x_i2c.h"
uint32_t fake_i2c_control[2],fake_i2c_status[2],fake_i2c_dma_control[2],fake_i2c_data[2],fake_i2c_rise[2];
void rcu_periph_clock_enable(uint32_t id) { fake_i2c_clock(id,1); }
void rcu_periph_clock_disable(uint32_t id) { fake_i2c_clock(id,0); }
uint32_t rcu_clock_freq_get(uint32_t id) { assert(id==CK_APB1);return pclk; }
void i2c_deinit(uint32_t id) { (void)id; }
void i2c_clock_config(uint32_t id,uint32_t speed,uint32_t duty) { (void)id;speed_seen=speed;duty_seen=duty; }
void i2c_mode_addr_config(uint32_t id,uint32_t mode,uint32_t format,uint32_t address) { (void)id;(void)mode;(void)format;(void)address; }
void i2c_enable(uint32_t id) { (void)id; }
void i2c_disable(uint32_t id) { (void)id; }
FlagStatus i2c_flag_get(uint32_t id,i2c_flag_enum flag) { return flags[id-1U][flag]?SET:RESET; }
void i2c_flag_clear(uint32_t id,i2c_flag_enum flag)
{
    uint32_t bit=flag==I2C_FLAG_LOSTARB?I2C_STAT0_LOSTARB:flag==I2C_FLAG_AERR?I2C_STAT0_AERR:
                 flag==I2C_FLAG_BERR?I2C_STAT0_BERR:flag==I2C_FLAG_OUERR?I2C_STAT0_OUERR:
                 flag==I2C_FLAG_SMBTO?I2C_STAT0_SMBTO:0U;
    if(flag==I2C_FLAG_ADDSEND)++addr_clears;else I2C_STAT0(id)&=~bit;
}
void i2c_start_on_bus(uint32_t id) { I2C_CTL0(id)|=I2C_CTL0_START; }
void i2c_stop_on_bus(uint32_t id) { I2C_CTL0(id)|=I2C_CTL0_STOP; }
void i2c_master_addressing(uint32_t id,uint32_t address,uint32_t direction) { (void)id;sent_address=address;sent_direction=direction; }
void i2c_ack_config(uint32_t id,uint32_t en) { if(en)I2C_CTL0(id)|=I2C_CTL0_ACKEN;else I2C_CTL0(id)&=~I2C_CTL0_ACKEN; }
void i2c_ackpos_config(uint32_t id,uint32_t n) { if(n)I2C_CTL0(id)|=I2C_CTL0_POAP;else I2C_CTL0(id)&=~I2C_CTL0_POAP; }
void i2c_data_transmit(uint32_t id,uint8_t value) { (void)id;sent_byte=value; }
uint8_t i2c_data_receive(uint32_t id) { (void)id;return 0x56U; }
static uint32_t *control(void) { return &fake_i2c_control[0]; }
static void set_errors(unsigned mask) { fake_i2c_status[0]=mask; }
static unsigned errors(void) { return fake_i2c_status[0]; }
static void observe_address(void) { }
static void observe_write(void) { }
static void set_read(uint8_t data) { assert(data==0x56U); }
#define I2C_CR1_START I2C_CTL0_START
#define I2C_CR1_STOP I2C_CTL0_STOP
#define I2C_CR1_ACK I2C_CTL0_ACKEN
#define I2C_CR1_POS I2C_CTL0_POAP
#define ERR_ARB I2C_STAT0_LOSTARB
#define ERR_NACK I2C_STAT0_AERR
#define ERR_BUS I2C_STAT0_BERR
#define ERR_TIMEOUT I2C_STAT0_SMBTO
#endif
int main(void)
{
    ABoxI2cAddress own7={0x2aU,ABOX_I2C_ADDRESS_7BIT},own10={0x2abU,ABOX_I2C_ADDRESS_10BIT};
#ifdef TEST_STM32
    assert(ABoxI2cHw_OwnAddress(own7)==0x4054U && ABoxI2cHw_OwnAddress(own10)==0xc2abU);
    assert(ABoxI2cHw_MaxPclk()==36000000U);
#else
    assert(ABoxI2cHw_OwnAddress(own7)==0x54U && ABoxI2cHw_OwnAddress(own10)==0x82abU);
    assert(ABoxI2cHw_MaxPclk()==54000000U);
#endif
    assert(ABoxI2cHw_Pclk()==36000000U);
    assert(ABoxI2cHw_Open(1,100000U,ABOX_I2C_DUTY_2)==ABOX_HAL_OK && clocks[1] && speed_seen==100000U && duty_seen==0U);
#ifdef TEST_STM32
    assert(fake_i2c_st[0].TRISE==37U && (fake_i2c_st[0].CR1&I2C_CR1_PE));
    pclk=6800000U;assert(ABoxI2cHw_Open(1,300000U,ABOX_I2C_DUTY_2)==ABOX_HAL_OK && fake_i2c_st[0].TRISE==3U);
    pclk=36000000U;
#else
    assert(fake_i2c_rise[0]==37U);
    pclk=6800000U;assert(ABoxI2cHw_Open(1,300000U,ABOX_I2C_DUTY_2)==ABOX_HAL_OK && fake_i2c_rise[0]==3U);
    pclk=36000000U;
#endif
#ifndef TEST_STM32
    pclk=54000000U;assert(ABoxI2cHw_Open(1,100000U,ABOX_I2C_DUTY_2)==ABOX_HAL_OK && fake_i2c_rise[0]==55U);
    pclk=36000000U;
#endif
    assert(ABoxI2cHw_DmaAddress(1)!=0U);ABoxI2cHw_DmaRequest(1,1,1);
#ifdef TEST_STM32
    assert((fake_i2c_st[0].CR2 & (I2C_CR2_DMAEN|I2C_CR2_LAST))==(I2C_CR2_DMAEN|I2C_CR2_LAST));
#else
    assert((fake_i2c_dma_control[0] & (I2C_CTL1_DMAON|I2C_CTL1_DMALST))==(I2C_CTL1_DMAON|I2C_CTL1_DMALST));
#endif
    ABoxI2cHw_DmaRequest(1,0,0);
    ABoxI2cHw_Start(1);assert(*control()&I2C_CR1_START);
    ABoxI2cHw_Stop(1);assert(ABoxI2cHw_Flag(1,ABOX_I2C_HW_STOP_PENDING));*control()&=~I2C_CR1_STOP;
    ABoxI2cHw_Ack(1,1);ABoxI2cHw_AckNext(1,1);assert((*control()&(I2C_CR1_ACK|I2C_CR1_POS))==(I2C_CR1_ACK|I2C_CR1_POS));
    ABoxI2cHw_Ack(1,0);ABoxI2cHw_AckNext(1,0);assert(!(*control()&(I2C_CR1_ACK|I2C_CR1_POS)));
    ABoxI2cHw_Address(1,0x2a,1);observe_address();assert(sent_address==0x54U && sent_direction==1U);
    ABoxI2cHw_Address(1,0x2a,0);observe_address();assert(sent_address==0x54U && sent_direction==0U);
    ABoxI2cHw_AddressByte(1,0xf4U);observe_write();assert(sent_byte==0xf4U);
#ifdef TEST_STM32
    flags[0][I2C_FLAG_ADD10]=1U;
#else
    flags[0][I2C_FLAG_ADD10SEND]=1U;
#endif
    assert(ABoxI2cHw_Flag(1,ABOX_I2C_HW_HEADER10));
    ABoxI2cHw_ClearAddress(1);assert(addr_clears==1U);
    ABoxI2cHw_Write(1,0x12);observe_write();assert(sent_byte==0x12U);set_read(0x56U);assert(ABoxI2cHw_Read(1)==0x56U);
    flags[0][ABOX_I2C_HW_START]=1U;assert(ABoxI2cHw_Flag(1,ABOX_I2C_HW_START));
    set_errors(ERR_NACK);assert(ABoxI2cHw_Error(1)==ABOX_HAL_NO_ACK && !errors());
    set_errors(ERR_ARB|ERR_BUS);assert(ABoxI2cHw_Error(1)==ABOX_HAL_ARBITRATION_LOST && !errors());
    set_errors(ERR_BUS);assert(ABoxI2cHw_Error(1)==ABOX_HAL_ERROR && !errors());
    set_errors(ERR_TIMEOUT);assert(ABoxI2cHw_Error(1)==ABOX_HAL_BUS_TIMEOUT && !errors());
    assert(ABoxI2cHw_Error(1)==ABOX_HAL_OK);
    assert(ABoxI2cHw_EnterCritical()==1U);ABoxI2cHw_LeaveCritical(1U);assert(primask==1U);
    assert(ABoxI2cHw_Close(1)==ABOX_HAL_OK && !clocks[1]);
    return 0;
}
