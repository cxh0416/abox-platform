#include "abox_i2c_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_i2c.h"

static uint32_t i2c(unsigned id) { return id == 1U ? I2C0 : I2C1; }
uintptr_t ABoxI2cHw_Base(unsigned id) { return (uintptr_t)i2c(id); }
uint32_t ABoxI2cHw_OwnAddress(ABoxI2cAddress a)
{ return a.mode == ABOX_I2C_ADDRESS_10BIT ? I2C_SADDR0_ADDFORMAT | a.value : (uint32_t)a.value << 1U; }
uint32_t ABoxI2cHw_Pclk(void) { return rcu_clock_freq_get(CK_APB1); }
uint32_t ABoxI2cHw_MaxPclk(void) { return 54000000U; }
ABoxHalStatus ABoxI2cHw_Open(unsigned id, uint32_t speed, ABoxI2cDuty duty)
{
    uint32_t instance = i2c(id);
    rcu_periph_clock_enable(id == 1U ? RCU_I2C0 : RCU_I2C1);
    i2c_deinit(instance);
    i2c_clock_config(instance, speed, duty == ABOX_I2C_DUTY_2 ? I2C_DTCY_2 : I2C_DTCY_16_9);
    /* Use the actual APB1 frequency for rise timing. This also avoids the
     * vendor's standard-mode cap of 54 when 55 is needed at 54 MHz. */
    {
        uint32_t pclk=ABoxI2cHw_Pclk();
        I2C_RT(instance)=speed<=100000U ? pclk/1000000U+1U : (uint32_t)(((uint64_t)pclk*3U)/10000000U)+1U;
    }
    i2c_mode_addr_config(instance, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, 0U);
    i2c_enable(instance);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2cHw_Close(unsigned id)
{
    i2c_disable(i2c(id)); i2c_deinit(i2c(id));
    rcu_periph_clock_disable(id == 1U ? RCU_I2C0 : RCU_I2C1);
    return ABOX_HAL_OK;
}
int ABoxI2cHw_Flag(unsigned id, ABoxI2cHwFlag flag)
{
    uint32_t instance = i2c(id);
    switch (flag) {
    case ABOX_I2C_HW_BUSY: return i2c_flag_get(instance, I2C_FLAG_I2CBSY) == SET;
    case ABOX_I2C_HW_START: return i2c_flag_get(instance, I2C_FLAG_SBSEND) == SET;
    case ABOX_I2C_HW_ADDRESS: return i2c_flag_get(instance, I2C_FLAG_ADDSEND) == SET;
    case ABOX_I2C_HW_HEADER10: return i2c_flag_get(instance, I2C_FLAG_ADD10SEND) == SET;
    case ABOX_I2C_HW_TX_EMPTY: return i2c_flag_get(instance, I2C_FLAG_TBE) == SET;
    case ABOX_I2C_HW_FINISHED: return i2c_flag_get(instance, I2C_FLAG_BTC) == SET;
    case ABOX_I2C_HW_RX_READY: return i2c_flag_get(instance, I2C_FLAG_RBNE) == SET;
    case ABOX_I2C_HW_STOP_PENDING: return (I2C_CTL0(instance) & I2C_CTL0_STOP) != 0U;
    default: return 0;
    }
}
ABoxHalStatus ABoxI2cHw_Error(unsigned id)
{
    uint32_t instance = i2c(id);
    uint32_t errors = I2C_STAT0(instance) & (I2C_STAT0_LOSTARB | I2C_STAT0_AERR | I2C_STAT0_BERR | I2C_STAT0_OUERR | I2C_STAT0_SMBTO);
    if (!errors) return ABOX_HAL_OK;
    if (errors & I2C_STAT0_LOSTARB) i2c_flag_clear(instance, I2C_FLAG_LOSTARB);
    if (errors & I2C_STAT0_AERR) i2c_flag_clear(instance, I2C_FLAG_AERR);
    if (errors & I2C_STAT0_BERR) i2c_flag_clear(instance, I2C_FLAG_BERR);
    if (errors & I2C_STAT0_OUERR) i2c_flag_clear(instance, I2C_FLAG_OUERR);
    if (errors & I2C_STAT0_SMBTO) i2c_flag_clear(instance, I2C_FLAG_SMBTO);
    if (errors & I2C_STAT0_LOSTARB) return ABOX_HAL_ARBITRATION_LOST;
    if (errors & I2C_STAT0_SMBTO) return ABOX_HAL_BUS_TIMEOUT;
    if (errors & (I2C_STAT0_BERR | I2C_STAT0_OUERR)) return ABOX_HAL_ERROR;
    return ABOX_HAL_NO_ACK;
}
void ABoxI2cHw_Start(unsigned id) { i2c_start_on_bus(i2c(id)); }
void ABoxI2cHw_Stop(unsigned id) { i2c_stop_on_bus(i2c(id)); }
void ABoxI2cHw_Address(unsigned id, uint8_t address, int read)
{ i2c_master_addressing(i2c(id), (uint32_t)address << 1U, read ? I2C_RECEIVER : I2C_TRANSMITTER); }
void ABoxI2cHw_AddressByte(unsigned id, uint8_t value) { i2c_data_transmit(i2c(id), value); }
void ABoxI2cHw_ClearAddress(unsigned id) { i2c_flag_clear(i2c(id), I2C_FLAG_ADDSEND); }
void ABoxI2cHw_Ack(unsigned id, int enabled) { i2c_ack_config(i2c(id), enabled ? I2C_ACK_ENABLE : I2C_ACK_DISABLE); }
void ABoxI2cHw_AckNext(unsigned id, int next) { i2c_ackpos_config(i2c(id), next ? I2C_ACKPOS_NEXT : I2C_ACKPOS_CURRENT); }
void ABoxI2cHw_Write(unsigned id, uint8_t value) { i2c_data_transmit(i2c(id), value); }
uint8_t ABoxI2cHw_Read(unsigned id) { return i2c_data_receive(i2c(id)); }
uint32_t ABoxI2cHw_EnterCritical(void) { uint32_t key = __get_PRIMASK(); __disable_irq(); return key; }
void ABoxI2cHw_LeaveCritical(uint32_t key) { __set_PRIMASK(key); }

uintptr_t ABoxI2cHw_DmaAddress(unsigned id) { return (uintptr_t)&I2C_DATA(i2c(id)); }
void ABoxI2cHw_DmaRequest(unsigned id,int enabled,int last)
{
    uint32_t a=i2c(id);
    if (enabled) { if (last) I2C_CTL1(a)|=I2C_CTL1_DMALST; else I2C_CTL1(a)&=~I2C_CTL1_DMALST; I2C_CTL1(a)|=I2C_CTL1_DMAON; }
    else I2C_CTL1(a)&=~(I2C_CTL1_DMAON|I2C_CTL1_DMALST);
}
