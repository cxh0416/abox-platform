#ifndef TEST_I2C_GD_H
#define TEST_I2C_GD_H
#include <stdint.h>
typedef enum { RESET=0, SET=1 } FlagStatus;
typedef enum { I2C_FLAG_I2CBSY=0,I2C_FLAG_SBSEND,I2C_FLAG_ADDSEND,I2C_FLAG_TBE,
               I2C_FLAG_BTC,I2C_FLAG_RBNE,I2C_FLAG_LOSTARB,I2C_FLAG_AERR,
               I2C_FLAG_BERR,I2C_FLAG_OUERR,I2C_FLAG_ADD10SEND,I2C_FLAG_SMBTO } i2c_flag_enum;
#define I2C0 1U
#define I2C1 2U
#define RCU_I2C0 1U
#define RCU_I2C1 2U
#define CK_APB1 1U
#define I2C_DTCY_2 0U
#define I2C_DTCY_16_9 1U
#define I2C_I2CMODE_ENABLE 0U
#define I2C_ADDFORMAT_7BITS 0U
#define I2C_SADDR0_ADDFORMAT (1U<<15)
#define I2C_RECEIVER 1U
#define I2C_TRANSMITTER 0U
#define I2C_ACK_ENABLE 1U
#define I2C_ACK_DISABLE 0U
#define I2C_ACKPOS_NEXT 1U
#define I2C_ACKPOS_CURRENT 0U
#define I2C_CTL1_DMAON (1U<<11)
#define I2C_CTL1_DMALST (1U<<12)
#define I2C_CTL0_START 1U
#define I2C_CTL0_STOP 2U
#define I2C_CTL0_ACKEN 4U
#define I2C_CTL0_POAP 8U
#define I2C_STAT0_LOSTARB 0x100U
#define I2C_STAT0_SMBTO 0x4000U
#define I2C_STAT0_AERR 0x200U
#define I2C_STAT0_BERR 0x400U
#define I2C_STAT0_OUERR 0x800U
extern uint32_t fake_i2c_control[2],fake_i2c_status[2],fake_i2c_dma_control[2],fake_i2c_data[2];
#define I2C_CTL1(id) (fake_i2c_dma_control[(id)-1U])
#define I2C_DATA(id) (fake_i2c_data[(id)-1U])
#define I2C_CTL0(id) (fake_i2c_control[(id)-1U])
#define I2C_STAT0(id) (fake_i2c_status[(id)-1U])
void rcu_periph_clock_enable(uint32_t id);
void rcu_periph_clock_disable(uint32_t id);
uint32_t rcu_clock_freq_get(uint32_t id);
void i2c_deinit(uint32_t id);
void i2c_clock_config(uint32_t id,uint32_t speed,uint32_t duty);
void i2c_mode_addr_config(uint32_t id,uint32_t mode,uint32_t format,uint32_t address);
void i2c_enable(uint32_t id);
void i2c_disable(uint32_t id);
FlagStatus i2c_flag_get(uint32_t id,i2c_flag_enum flag);
void i2c_flag_clear(uint32_t id,i2c_flag_enum flag);
void i2c_start_on_bus(uint32_t id);
void i2c_stop_on_bus(uint32_t id);
void i2c_master_addressing(uint32_t id,uint32_t address,uint32_t direction);
void i2c_ack_config(uint32_t id,uint32_t enabled);
void i2c_ackpos_config(uint32_t id,uint32_t next);
void i2c_data_transmit(uint32_t id,uint8_t value);
uint8_t i2c_data_receive(uint32_t id);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t key);
#endif
