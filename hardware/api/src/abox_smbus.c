#include <string.h>
#include "abox_smbus.h"
#include "abox_i2c.h"
#include "abox_i2c_counted.h"
static uint8_t crc_byte(uint8_t crc,uint8_t value)
{
    crc^=value;
    for (unsigned i=0U;i<8U;++i) crc=(uint8_t)((crc&0x80U) ? (crc<<1U)^0x07U : crc<<1U);
    return crc;
}
ABoxHalStatus ABoxSmbus_Quick(unsigned instance,uint8_t address,uint8_t read_direction,uint32_t timeout_ms)
{ return ABoxI2c_Quick(instance,address,read_direction,timeout_ms); }
ABoxHalStatus ABoxSmbus_SetHardwareMode(unsigned instance,uint8_t enabled)
{ return ABoxI2c_SetSmbusMode(instance,enabled); }
ABoxHalStatus ABoxSmbus_AlertPending(unsigned instance,uint8_t clear,uint8_t *pending)
{ return ABoxI2c_SmbusAlert(instance,clear,pending); }
ABoxHalStatus ABoxSmbus_Transfer(unsigned instance,uint8_t address,
                                 const uint8_t *write,uint8_t write_length,
                                 uint8_t *read,uint8_t read_length,uint8_t pec,
                                 uint32_t timeout_ms)
{
    uint8_t tx[35],rx[33],crc=0U;ABoxHalStatus result;
    ABoxI2cAddress target={address,ABOX_I2C_ADDRESS_7BIT};
    if (address>0x7fU || pec>1U || write_length>34U || read_length>32U ||
        (!write_length && !read_length) || (write_length && !write) || (read_length && !read)) return ABOX_HAL_INVALID;
    if (write_length) {
        memcpy(tx,write,write_length);crc=crc_byte(crc,(uint8_t)(address<<1U));
        for (unsigned i=0U;i<write_length;++i) crc=crc_byte(crc,tx[i]);
        if (pec && !read_length) tx[write_length]=crc;
    }
    result=ABoxI2c_Transfer(instance,target,write_length ? tx : 0,
                            (uint16_t)(write_length+(pec && write_length && !read_length ? 1U : 0U)),
                            read_length ? rx : 0,(uint16_t)(read_length+(pec && read_length ? 1U : 0U)),timeout_ms);
    if (result!=ABOX_HAL_OK) return result;
    if (read_length && pec) {
        crc=crc_byte(crc,(uint8_t)((address<<1U)|1U));
        for (unsigned i=0U;i<read_length;++i) crc=crc_byte(crc,rx[i]);
        if (crc!=rx[read_length]) return ABOX_HAL_ERROR;
    }
    if (read_length) memcpy(read,rx,read_length);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxSmbus_ReadAlertResponse(unsigned instance,uint8_t pec,uint8_t *response,uint32_t timeout_ms)
{
    if (!response) return ABOX_HAL_INVALID;
    return ABoxSmbus_Transfer(instance,0x0cU,0,0U,response,1U,pec,timeout_ms);
}
static ABoxHalStatus block_read(unsigned id,uint8_t address,const uint8_t *tx,uint8_t txlen,
                                uint8_t *read,uint8_t capacity,uint8_t *count,uint8_t pec,uint32_t timeout)
{
    uint8_t rx[33],n=0U,crc=0U;ABoxHalStatus result;
    if (address>0x7fU || !read || !count || capacity<1U || capacity>32U || pec>1U) return ABOX_HAL_INVALID;
    result=ABoxI2c_TransferCounted(id,address,tx,txlen,rx,capacity,pec,&n,timeout);
    if (result!=ABOX_HAL_OK) return result;
    if (pec) {
        crc=crc_byte(crc,(uint8_t)(address<<1U));
        for (unsigned i=0U;i<txlen;++i) crc=crc_byte(crc,tx[i]);
        crc=crc_byte(crc,(uint8_t)((address<<1U)|1U));crc=crc_byte(crc,n);
        for (unsigned i=0U;i<n;++i) crc=crc_byte(crc,rx[i]);
        if (crc!=rx[n]) return ABOX_HAL_ERROR;
    }
    memcpy(read,rx,n);*count=n;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxSmbus_BlockRead(unsigned id,uint8_t address,uint8_t command,
                                 uint8_t *read,uint8_t capacity,uint8_t *count,uint8_t pec,uint32_t timeout)
{ return block_read(id,address,&command,1U,read,capacity,count,pec,timeout); }
ABoxHalStatus ABoxSmbus_BlockProcessCall(unsigned id,uint8_t address,uint8_t command,
                                        const uint8_t *write,uint8_t write_length,
                                        uint8_t *read,uint8_t capacity,uint8_t *count,uint8_t pec,uint32_t timeout)
{
    uint8_t tx[34];
    if (!write || !write_length || write_length>32U) return ABOX_HAL_INVALID;
    tx[0]=command;tx[1]=write_length;memcpy(&tx[2],write,write_length);
    return block_read(id,address,tx,(uint8_t)(write_length+2U),read,capacity,count,pec,timeout);
}
typedef struct {
    const ABoxSmbusHostNotifyEvents *events;
    ABoxI2cSlaveEvents slave;
    uint8_t bytes[3],count,valid,started;
} HostNotify;
static HostNotify host[2];
static void host_addressed(void *ctx,unsigned id,ABoxI2cSlaveDirection dir,ABoxI2cSlaveMatch match)
{
    HostNotify *h=ctx;(void)id;h->count=0U;
    h->valid=(uint8_t)(dir==ABOX_I2C_SLAVE_WRITE && match==ABOX_I2C_SLAVE_PRIMARY);
}
static void host_received(void *ctx,unsigned id,uint8_t value)
{
    HostNotify *h=ctx;(void)id;
    if (h->count<3U) h->bytes[h->count++]=value;else h->valid=0U;
}
static void host_ended(void *ctx,unsigned id,ABoxI2cSlaveEnd reason)
{
    HostNotify *h=ctx;const ABoxSmbusHostNotifyEvents *e=h->events;
    if (!e) return;
    if (h->valid && h->count==3U && reason==ABOX_I2C_SLAVE_STOP)
        e->notify(e->context,id,(uint8_t)(h->bytes[0]>>1U),(uint16_t)(h->bytes[1]|((uint16_t)h->bytes[2]<<8U)));
    else if (e->error) e->error(e->context,id,ABOX_HAL_ERROR);
    h->valid=0U;h->count=0U;
}
static void host_error(void *ctx,unsigned id,ABoxHalStatus status)
{
    HostNotify *h=ctx;h->valid=0U;h->count=0U;
    if (h->events && h->events->error) h->events->error(h->events->context,id,status);
}
static void host_alert(void *ctx,unsigned id)
{ HostNotify *h=ctx;if (h->events && h->events->alert) h->events->alert(h->events->context,id); }
ABoxHalStatus ABoxSmbus_HostNotifyStart(unsigned id,const ABoxSmbusHostNotifyEvents *events)
{
    HostNotify *h;ABoxHalStatus result;
    ABoxI2cSlaveConfig c={{0x08U,ABOX_I2C_ADDRESS_7BIT},0U,0U,0U,1U};
    if (id<1U || id>2U || !events || !events->notify) return ABOX_HAL_INVALID;
    h=&host[id-1U];if (h->started) return ABOX_HAL_BUSY;
    memset(h,0,sizeof(*h));h->events=events;
    h->slave.context=h;h->slave.addressed=host_addressed;h->slave.received=host_received;
    h->slave.ended=host_ended;h->slave.error=host_error;h->slave.smbus_alert=host_alert;
    result=ABoxI2c_SlaveStart(id,&c,&h->slave);
    if (result==ABOX_HAL_OK) h->started=1U;else h->events=0;
    return result;
}
ABoxHalStatus ABoxSmbus_HostNotifyStop(unsigned id)
{
    HostNotify *h;ABoxHalStatus result;
    if (id<1U || id>2U) return ABOX_HAL_INVALID;
    h=&host[id-1U];if (!h->started) return ABOX_HAL_INVALID;
    result=ABoxI2c_SlaveStop(id);if (result==ABOX_HAL_OK) { h->started=0U;h->events=0; }
    return result;
}
ABoxHalStatus ABoxSmbus_HostNotifyAbort(unsigned id)
{
    HostNotify *h;ABoxHalStatus result;
    if (id<1U || id>2U) return ABOX_HAL_INVALID;
    h=&host[id-1U];if (!h->started) return ABOX_HAL_INVALID;
    result=ABoxI2c_SlaveAbort(id);if (result==ABOX_HAL_OK) { h->started=0U;h->events=0; }
    return result;
}
