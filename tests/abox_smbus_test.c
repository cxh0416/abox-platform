#include <assert.h>
#include <string.h>
#include "abox_smbus.h"
#include "abox_i2c.h"
#include "abox_i2c_counted.h"
static uint8_t tx[35],rx_data[33];static unsigned tx_len,rx_len,calls;
static ABoxHalStatus outcome=ABOX_HAL_OK;
static const ABoxI2cSlaveEvents *slave;static unsigned notices,host_errors;static uint8_t noticed_address;static uint16_t noticed_word;
static ABoxHalStatus stop_result=ABOX_HAL_OK;
static uint8_t counted_n=2U;
ABoxHalStatus ABoxI2c_TransferCounted(unsigned id,uint8_t address,const uint8_t *write,uint8_t wn,
                                      uint8_t *read,uint8_t capacity,uint8_t trailing,uint8_t *count,uint32_t timeout)
{
    assert(id==1U && address==0x5aU && timeout==50U && capacity>=counted_n);
    tx_len=wn;memcpy(tx,write,wn);memcpy(read,rx_data,counted_n+trailing);*count=counted_n;return outcome;
}
ABoxHalStatus ABoxI2c_SmbusAlert(unsigned id,uint8_t clear,uint8_t *pending)
{ assert(id==1U && clear==1U && pending);*pending=1U;return ABOX_HAL_OK; }
ABoxHalStatus ABoxI2c_SetSmbusMode(unsigned id,uint8_t enabled)
{ assert(id==1U && enabled==1U);return ABOX_HAL_OK; }
ABoxHalStatus ABoxI2c_Quick(unsigned id,uint8_t address,uint8_t direction,uint32_t timeout)
{ assert(id==1U && address==0x50U && direction==1U && timeout==50U);return ABOX_HAL_OK; }
ABoxHalStatus ABoxI2c_SlaveStart(unsigned id,const ABoxI2cSlaveConfig *c,const ABoxI2cSlaveEvents *e)
{ assert(id==1U && c->own_address.value==8U && c->clock_stretch);slave=e;return ABOX_HAL_OK; }
ABoxHalStatus ABoxI2c_SlaveStop(unsigned id) { assert(id==1U);return stop_result; }
ABoxHalStatus ABoxI2c_SlaveAbort(unsigned id) { assert(id==1U);return ABOX_HAL_OK; }
static void notice(void *context,unsigned id,uint8_t address,uint16_t word)
{ assert(context==(void *)0x1234 && id==1U);++notices;noticed_address=address;noticed_word=word; }
static void host_error(void *context,unsigned id,ABoxHalStatus status)
{ assert(context==(void *)0x1234 && id==1U && status==ABOX_HAL_ERROR);++host_errors; }
static void host_alert_event(void *context,unsigned id)
{ assert(context==(void *)0x1234 && id==1U);++host_errors; }
ABoxHalStatus ABoxI2c_Transfer(unsigned id,ABoxI2cAddress address,const uint8_t *write,uint16_t wn,
                               uint8_t *read,uint16_t rn,uint32_t timeout)
{
    assert(id==1U && address.mode==ABOX_I2C_ADDRESS_7BIT && timeout==50U);
    (void)address;tx_len=wn;rx_len=rn;++calls;
    if (wn) memcpy(tx,write,wn);if (rn) memcpy(read,rx_data,rn);return outcome;
}
int main(void)
{
    uint8_t write[2]={0x10U,0x33U},read[2]={0U,0U},command=0x0dU;
    assert(ABoxSmbus_SetHardwareMode(1U,1U)==ABOX_HAL_OK);
    assert(ABoxSmbus_Quick(1U,0x50U,1U,50U)==ABOX_HAL_OK);
    assert(ABoxSmbus_Transfer(1U,0x50U,write,2U,0,0,1U,50U)==ABOX_HAL_OK);
    assert(tx_len==3U && !rx_len && tx[0]==0x10U && tx[1]==0x33U && tx[2]==0x86U);
    rx_data[0]=0xdfU;rx_data[1]=0x02U;rx_data[2]=0xedU;
    assert(ABoxSmbus_Transfer(1U,0x5aU,&command,1U,read,2U,1U,50U)==ABOX_HAL_OK);
    assert(tx_len==1U && rx_len==3U && tx[0]==command && read[0]==0xdfU && read[1]==2U);
    read[0]=0xaaU;read[1]=0xbbU;rx_data[2]=0;
    assert(ABoxSmbus_Transfer(1U,0x5aU,&command,1U,read,2U,1U,50U)==ABOX_HAL_ERROR);
    assert(read[0]==0xaaU && read[1]==0xbbU);
    rx_data[2]=0x5dU;
    assert(ABoxSmbus_Transfer(1U,0x5aU,0,0U,read,2U,1U,50U)==ABOX_HAL_OK && !tx_len);
    outcome=ABOX_HAL_NO_ACK;read[0]=0x55U;
    assert(ABoxSmbus_Transfer(1U,0x5aU,0,0U,read,2U,0U,50U)==ABOX_HAL_NO_ACK && read[0]==0x55U);
    assert(ABoxSmbus_Transfer(1U,0x80U,write,2U,0,0U,0U,50U)==ABOX_HAL_INVALID);
    assert(ABoxSmbus_Transfer(1U,0x50U,write,35U,0,0U,0U,50U)==ABOX_HAL_INVALID);
    outcome=ABOX_HAL_OK;rx_data[0]=0x9bU;
    assert(ABoxSmbus_ReadAlertResponse(1U,0U,read,50U)==ABOX_HAL_OK && read[0]==0x9bU);
    rx_data[1]=0x22U;assert(ABoxSmbus_ReadAlertResponse(1U,1U,read,50U)==ABOX_HAL_OK && read[0]==0x9bU);
    assert(ABoxSmbus_ReadAlertResponse(1U,0U,0,50U)==ABOX_HAL_INVALID);
    assert(calls==7U);
    {
        uint8_t count=0U,payload[2]={0x10U,0x33U};
        rx_data[0]=0xdfU;rx_data[1]=2U;rx_data[2]=0x6eU;
        assert(ABoxSmbus_BlockRead(1U,0x5aU,0x0dU,read,2U,&count,1U,50U)==ABOX_HAL_OK);
        assert(count==2U && read[0]==0xdfU && read[1]==2U && tx_len==1U && tx[0]==0x0dU);
        count=77U;read[0]=0xaaU;rx_data[2]=0U;
        assert(ABoxSmbus_BlockRead(1U,0x5aU,0x0dU,read,2U,&count,1U,50U)==ABOX_HAL_ERROR);
        assert(count==77U && read[0]==0xaaU);
        rx_data[2]=0x38U;
        assert(ABoxSmbus_BlockProcessCall(1U,0x5aU,0x0dU,payload,2U,read,2U,&count,1U,50U)==ABOX_HAL_OK);
        assert(tx_len==4U && tx[0]==0x0dU && tx[1]==2U && tx[2]==0x10U && tx[3]==0x33U && count==2U);
    }
    {
        uint8_t pending=0U;ABoxSmbusHostNotifyEvents e={(void *)0x1234,notice,host_error,host_alert_event};
        assert(ABoxSmbus_AlertPending(1U,1U,&pending)==ABOX_HAL_OK && pending);
        assert(ABoxSmbus_HostNotifyStart(1U,&e)==ABOX_HAL_OK && slave);
        assert(ABoxSmbus_HostNotifyStart(1U,&e)==ABOX_HAL_BUSY);
        slave->addressed(slave->context,1U,ABOX_I2C_SLAVE_WRITE,ABOX_I2C_SLAVE_PRIMARY);
        slave->received(slave->context,1U,0x9bU);slave->received(slave->context,1U,0x34U);
        slave->received(slave->context,1U,0x12U);slave->ended(slave->context,1U,ABOX_I2C_SLAVE_STOP);
        assert(notices==1U && noticed_address==0x4dU && noticed_word==0x1234U && !host_errors);
        slave->addressed(slave->context,1U,ABOX_I2C_SLAVE_WRITE,ABOX_I2C_SLAVE_PRIMARY);
        slave->received(slave->context,1U,1U);slave->ended(slave->context,1U,ABOX_I2C_SLAVE_STOP);
        assert(notices==1U && host_errors==1U);
        slave->smbus_alert(slave->context,1U);assert(host_errors==2U);
        stop_result=ABOX_HAL_BUSY;assert(ABoxSmbus_HostNotifyStop(1U)==ABOX_HAL_BUSY);
        assert(ABoxSmbus_HostNotifyAbort(1U)==ABOX_HAL_OK);
        assert(ABoxSmbus_HostNotifyStart(1U,&e)==ABOX_HAL_OK);
        stop_result=ABOX_HAL_OK;assert(ABoxSmbus_HostNotifyStop(1U)==ABOX_HAL_OK);
        assert(ABoxSmbus_HostNotifyStop(1U)==ABOX_HAL_INVALID);
    }
    return 0;
}
