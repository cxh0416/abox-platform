#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_i2c_backend.h"
#include "abox_i2c_counted.h"
#include "abox_i2c_f105_regs.h"
#include "abox_platform_port.h"
#include "abox_dma.h"

static char trace[512];
static unsigned length, tick_ready=1, busy, fail_flag=255U, reads, primask, clock_hz=36000000U,max_clock_hz=36000000U;
static uint8_t address_bytes[8];
static unsigned address_length;
static uint8_t read_script[35];static unsigned script_length,script_position;
static ABoxI2cF105Registers slave_hw[2];
static unsigned slave_addresses, slave_received, slave_ends, slave_errors;
static unsigned slave_alerts;
static unsigned start_simulation, simulation_pending, simulation_step;
static ABoxI2cSlaveDirection slave_direction;
static ABoxI2cSlaveMatch slave_match;
static ABoxI2cSlaveEnd slave_end_reason;
static ABoxHalStatus slave_error;
static uint32_t now, configured_hz;
static ABoxI2cDuty configured_duty;
static ABoxHalStatus injected, address_error;
static const ABoxDmaEvents *pending_dma;
static ABoxDmaConfig dma_config[8];
static unsigned dma_owned[8], dma_channel, dma_enabled, dma_conflict, dma_fail, dma_timeout;
static uintptr_t dma_destination;
static uint16_t dma_count;
static void fire_dma_if_ready(void);
ABoxHalStatus ABoxDma_Open(unsigned ctl,unsigned ch,const ABoxDmaConfig *c)
{ assert(ctl==1U && ch>=4U && ch<=7U);if(ch==dma_conflict)return ABOX_HAL_BUSY;assert(!dma_owned[ch]);dma_owned[ch]=1U;dma_config[ch]=*c;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Start(unsigned ctl,unsigned ch,uintptr_t src,uintptr_t dst,uint16_t count,const ABoxDmaEvents *e)
{ assert(ctl==1U && dma_owned[ch] && src && dst && count);pending_dma=e;dma_channel=ch;dma_destination=dst;dma_count=count;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Abort(unsigned ctl,unsigned ch)
{ assert(ctl==1U && dma_owned[ch]);pending_dma=0;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned ctl,unsigned ch)
{ assert(ctl==1U && dma_owned[ch]);dma_owned[ch]=0U;return ABOX_HAL_OK; }
uintptr_t ABoxI2cHw_DmaAddress(unsigned id) { return 0x40005410U+id*1024U; }
void ABoxI2cHw_DmaRequest(unsigned id,int enabled,int last)
{ (void)id;(void)last;dma_enabled=(unsigned)enabled; }
static void fire_dma_if_ready(void)
{
    const ABoxDmaEvents *e=pending_dma;unsigned ch=dma_channel;
    if (!e || !dma_enabled || primask || dma_timeout) return;
    pending_dma=0;
    if (!dma_fail && dma_config[ch].direction==ABOX_DMA_PERIPHERAL_TO_MEMORY)
        for (unsigned i=0U;i<dma_count;++i)((uint8_t *)dma_destination)[i]=(uint8_t)(0x90U+i);
    e->on_event(e->context,1U,ch,dma_fail ? ABOX_DMA_TRANSFER_ERROR : ABOX_DMA_COMPLETE);
}

static void log_op(char c) { assert(length+1U<sizeof(trace));trace[length++]=c;trace[length]=0; }
static uint32_t fake_tick(void *ctx) { (void)ctx;return now; }
static ABoxPlatformPort port={.get_tick_ms=fake_tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return tick_ready?&port:0; }
uint32_t ABox_PortGetTickMs(void)
{
    if (simulation_pending && !primask && (start_simulation==1U || start_simulation==3U)) {
        if (simulation_step==0U) {
            slave_hw[0].status1=ABOX_I2C_SR1_ADDR;slave_hw[0].status2=0U;ABoxI2c_Irq(1U);
        } else if (simulation_step==1U) {
            slave_hw[0].status1=ABOX_I2C_SR1_RXNE;ABoxI2c_Irq(1U);
        } else if (simulation_step==2U) {
            slave_hw[0].status1=ABOX_I2C_SR1_STOP | (start_simulation==3U ? ABOX_I2C_SR1_SB : 0U);
            ABoxI2c_Irq(1U);
            if (start_simulation==3U) { slave_hw[0].status1=ABOX_I2C_SR1_SB;simulation_pending=0U; }
        } else {
            slave_hw[0].status1=ABOX_I2C_SR1_SB;ABoxI2c_Irq(1U);simulation_pending=0U;
        }
        if (simulation_pending) slave_hw[0].status1=0U;
        ++simulation_step;
    }
    return now++;
}
uint32_t ABoxI2cHw_Pclk(void) { return clock_hz; }
uint32_t ABoxI2cHw_MaxPclk(void) { return max_clock_hz; }
uintptr_t ABoxI2cHw_Base(unsigned id) { return (uintptr_t)&slave_hw[id-1U]; }
uint32_t ABoxI2cHw_OwnAddress(ABoxI2cAddress a)
{ return 0x4000U | (a.mode==ABOX_I2C_ADDRESS_10BIT ? 0x8000U|a.value : (uint32_t)a.value<<1U); }
ABoxHalStatus ABoxI2cHw_Open(unsigned id,uint32_t speed,ABoxI2cDuty duty)
{ assert(id==1U||id==2U);configured_hz=speed;configured_duty=duty;injected=ABOX_HAL_OK;return ABOX_HAL_OK; }
ABoxHalStatus ABoxI2cHw_Close(unsigned id) { assert(id==1U||id==2U);return ABOX_HAL_OK; }
int ABoxI2cHw_Flag(unsigned id,ABoxI2cHwFlag flag)
{
    assert(id==1U||id==2U);
    if ((unsigned)flag==fail_flag) return flag==ABOX_I2C_HW_BUSY || flag==ABOX_I2C_HW_STOP_PENDING;
    if (flag==ABOX_I2C_HW_BUSY) return busy;
    if (flag==ABOX_I2C_HW_STOP_PENDING) return 0;
    return 1;
}
ABoxHalStatus ABoxI2cHw_Error(unsigned id)
{ ABoxHalStatus result=injected;(void)id;injected=ABOX_HAL_OK;return result; }
void ABoxI2cHw_Start(unsigned id)
{
    log_op('S');
    if (slave_hw[id-1U].control2 & ABOX_I2C_CR2_IRQ) {
        slave_hw[id-1U].control1 |= ABOX_I2C_CR1_START;
        if (start_simulation) { simulation_pending=1U;simulation_step=0U; }
        else slave_hw[id-1U].status1=ABOX_I2C_SR1_SB;
    }
}
void ABoxI2cHw_Stop(unsigned id) { (void)id;log_op('T'); }
void ABoxI2cHw_Address(unsigned id,uint8_t address,int read)
{ slave_hw[id-1U].status1 &= ~ABOX_I2C_SR1_SB;assert(address==0x2aU);log_op(read?'R':'W');injected=address_error; }
void ABoxI2cHw_AddressByte(unsigned id,uint8_t value)
{
    (void)id;assert(address_length<sizeof(address_bytes));address_bytes[address_length++]=value;
    log_op((value & 0xf8U)==0xf0U ? ((value&1U)?'h':'H') : 'L');
    injected=address_error;
}
void ABoxI2cHw_ClearAddress(unsigned id) { (void)id;log_op('C');fire_dma_if_ready(); }
void ABoxI2cHw_Ack(unsigned id,int enabled) { (void)id;log_op(enabled?'A':'a'); }
void ABoxI2cHw_AckNext(unsigned id,int next) { (void)id;log_op(next?'N':'n'); }
void ABoxI2cHw_Write(unsigned id,uint8_t value) { (void)id;assert(value>=0x10U);log_op('w'); }
uint8_t ABoxI2cHw_Read(unsigned id) { (void)id;log_op('r');if(script_position<script_length)return read_script[script_position++];return (uint8_t)++reads; }
uint32_t ABoxI2cHw_EnterCritical(void) { uint32_t old=primask;primask=1;log_op('[');return old; }
void ABoxI2cHw_LeaveCritical(uint32_t key) { assert(primask==1U);primask=key;log_op(']');fire_dma_if_ready(); }
static void reset_trace(void) { length=0;trace[0]=0;reads=0;address_length=0; }
static void slave_addressed(void *ctx,unsigned id,ABoxI2cSlaveDirection d,ABoxI2cSlaveMatch m)
{ assert(ctx==(void *)1 && id==1U);++slave_addresses;slave_direction=d;slave_match=m; }
static void slave_read(void *ctx,unsigned id,uint8_t value)
{ assert(ctx==(void *)1 && id==1U && value>=1U);++slave_received; }
static uint8_t slave_write(void *ctx,unsigned id) { assert(ctx==(void *)1 && id==1U);return 0x55U; }
static void slave_ended(void *ctx,unsigned id,ABoxI2cSlaveEnd reason)
{ assert(ctx==(void *)1 && id==1U);++slave_ends;slave_end_reason=reason; }
static void slave_failed(void *ctx,unsigned id,ABoxHalStatus status)
{ assert(ctx==(void *)1 && id==1U);++slave_errors;slave_error=status; }
static void slave_alert(void *ctx,unsigned id) { assert(ctx==(void *)1 && id==1U);++slave_alerts; }

int main(void)
{
    ABoxI2cConfig c={333000U,ABOX_I2C_DUTY_2};
    const ABoxI2cAddress a7={0x2aU,ABOX_I2C_ADDRESS_7BIT}, a10={0x2abU,ABOX_I2C_ADDRESS_10BIT};
    const ABoxI2cAddress bad7={0x80U,ABOX_I2C_ADDRESS_7BIT}, bad10={0x400U,ABOX_I2C_ADDRESS_10BIT};
    uint8_t tx[]={0x10,0x11,0x12,0x13},rx[8]={0};
    assert(ABoxI2c_Open(0,&c)==ABOX_HAL_INVALID);
    tick_ready=0;assert(ABoxI2c_Open(1,&c)==ABOX_HAL_INVALID);tick_ready=1;
    c.maximum_hz=400001U;assert(ABoxI2c_Open(1,&c)==ABOX_HAL_INVALID);c.maximum_hz=333000U;
    clock_hz=3500000U;assert(ABoxI2c_Open(1,&c)==ABOX_HAL_OK && configured_hz<=100000U);
    assert(ABoxI2c_Close(1U)==ABOX_HAL_OK);clock_hz=36000000U;
    clock_hz=54000000U;assert(ABoxI2c_Open(1,&c)==ABOX_HAL_UNSUPPORTED);
    max_clock_hz=54000000U;assert(ABoxI2c_Open(1,&c)==ABOX_HAL_OK && configured_hz<=333000U);
    assert(ABoxI2c_Close(1U)==ABOX_HAL_OK);clock_hz=36000000U;max_clock_hz=36000000U;
    assert(ABoxI2c_Open(1,&c)==ABOX_HAL_OK && configured_hz==324324U && configured_duty==ABOX_I2C_DUTY_2);
    assert(ABoxI2c_Open(1,&c)==ABOX_HAL_BUSY);
    assert(ABoxI2c_SetSmbusMode(1U,1U)==ABOX_HAL_OK && (slave_hw[0].control1 & 0xaU)==0xaU);
    {
        uint8_t pending=0U;slave_hw[0].status1=ABOX_I2C_SR1_SMBALERT;
        assert(ABoxI2c_SmbusAlert(1U,1U,&pending)==ABOX_HAL_OK && pending);
        assert(slave_hw[0].status1==~ABOX_I2C_SR1_SMBALERT);slave_hw[0].status1=0U;
    }
    busy=1U;assert(ABoxI2c_SetSmbusMode(1U,0U)==ABOX_HAL_BUSY);busy=0U;
    assert(ABoxI2c_SetSmbusMode(1U,0U)==ABOX_HAL_OK && !(slave_hw[0].control1 & 0xaU));
    reset_trace();assert(ABoxI2c_Quick(1U,0x2aU,0U,10U)==ABOX_HAL_OK && !strcmp(trace,"anSW[CT]A"));
    reset_trace();assert(ABoxI2c_Quick(1U,0x2aU,1U,10U)==ABOX_HAL_OK && !strcmp(trace,"anSR[CT]A"));
    assert(ABoxI2c_Quick(1U,0x80U,0U,10U)==ABOX_HAL_INVALID);
    {
        uint8_t out[33]={0},n=0U,cmd=0x10U;
        read_script[0]=1U;read_script[1]=0x55U;script_length=2U;script_position=0U;
        reset_trace();assert(ABoxI2c_TransferCounted(1U,0x2aU,&cmd,1U,out,32U,0U,&n,30U)==ABOX_HAL_OK);
        assert(n==1U && out[0]==0x55U && script_position==2U && !primask);
        read_script[0]=1U;read_script[1]=0x66U;read_script[2]=0x77U;script_length=3U;script_position=0U;
        reset_trace();assert(ABoxI2c_TransferCounted(1U,0x2aU,&cmd,1U,out,32U,1U,&n,30U)==ABOX_HAL_OK);
        assert(n==1U && out[0]==0x66U && out[1]==0x77U);
        read_script[0]=3U;read_script[1]=1U;read_script[2]=2U;read_script[3]=3U;script_length=4U;script_position=0U;
        reset_trace();assert(ABoxI2c_TransferCounted(1U,0x2aU,&cmd,1U,out,32U,0U,&n,30U)==ABOX_HAL_OK);
        assert(n==3U && out[0]==1U && out[2]==3U);
        read_script[0]=0U;script_length=1U;script_position=0U;n=99U;
        assert(ABoxI2c_TransferCounted(1U,0x2aU,&cmd,1U,out,32U,0U,&n,30U)==ABOX_HAL_ERROR && n==99U);
        script_length=script_position=0U;assert(ABoxI2c_Close(1U)==ABOX_HAL_OK && ABoxI2c_Open(1U,&c)==ABOX_HAL_OK);
        slave_hw[0].status1=ABOX_I2C_SR1_BTF;reset_trace();
        assert(ABoxI2c_TransferCounted(1U,0x2aU,&cmd,1U,out,32U,0U,&n,30U)==ABOX_HAL_ERROR);
        slave_hw[0].status1=0U;assert(ABoxI2c_Close(1U)==ABOX_HAL_OK && ABoxI2c_Open(1U,&c)==ABOX_HAL_OK);
    }
    assert(ABoxI2c_Transfer(1,a7,tx,0,rx,0,10)==ABOX_HAL_INVALID);
    assert(ABoxI2c_Transfer(1,a7,tx,2,tx+1,2,10)==ABOX_HAL_INVALID);
    assert(ABoxI2c_Transfer(1,bad7,tx,1,0,0,10)==ABOX_HAL_INVALID);
    busy=1;now=0xfffffffeU;assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,2)==ABOX_HAL_BUSY);busy=0;
    reset_trace();assert(ABoxI2c_Transfer(1,a7,tx,4,0,0,10)==ABOX_HAL_OK);
    assert(strcmp(trace,"AnSWCwwwwTAn")==0);
    reset_trace();assert(ABoxI2c_Transfer(1,a7,0,0,rx,1,10)==ABOX_HAL_OK);
    assert(strcmp(trace,"AnSRa[CT]rAn")==0 && rx[0]==1U && !primask);
    reset_trace();assert(ABoxI2c_Transfer(1,a7,0,0,rx,2,10)==ABOX_HAL_OK);
    assert(strcmp(trace,"AnSNR[Ca][Tr]rAn")==0 && rx[0]==1U && rx[1]==2U && !primask);
    reset_trace();assert(ABoxI2c_Transfer(1,a7,tx,4,rx,5,10)==ABOX_HAL_OK);
    assert(strcmp(trace,"AnSWCwwwwSRCrrar[Tr]rAn")==0);
    for(unsigned i=0;i<5U;++i)assert(rx[i]==i+1U);
    assert(ABoxI2c_Transfer(1,bad10,tx,1,0,0,10)==ABOX_HAL_INVALID);
    reset_trace();assert(ABoxI2c_Transfer(1,a10,tx,2,0,0,20)==ABOX_HAL_OK);
    assert(strcmp(trace,"AnSHLCwwTAn")==0 && address_length==2U && address_bytes[0]==0xf4U && address_bytes[1]==0xabU);
    reset_trace();assert(ABoxI2c_Transfer(1,a10,0,0,rx,1,20)==ABOX_HAL_OK);
    assert(strcmp(trace,"AnSHLCSha[CT]rAn")==0 && address_length==3U && address_bytes[2]==0xf5U);
    reset_trace();assert(ABoxI2c_Transfer(1,a10,tx,2,rx,5,20)==ABOX_HAL_OK);
    assert(strcmp(trace,"AnSHLCwwShCrrar[Tr]rAn")==0 && address_length==3U && address_bytes[2]==0xf5U);
    reset_trace();assert(ABoxI2c_TransferDma(1,a10,tx,4,rx,5,30)==ABOX_HAL_OK);
    assert(address_length==3U && address_bytes[0]==0xf4U && address_bytes[1]==0xabU && address_bytes[2]==0xf5U);
    reset_trace();assert(ABoxI2c_TransferDma(1,a10,0,0,rx,2,30)==ABOX_HAL_OK);
    assert(address_length==3U && address_bytes[2]==0xf5U && rx[1]==0x91U);
    fail_flag=ABOX_I2C_HW_HEADER10;reset_trace();now=0U;
    assert(ABoxI2c_Transfer(1,a10,tx,1,0,0,3)==ABOX_HAL_TIMEOUT && strchr(trace,'T'));
    fail_flag=255U;assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
    address_error=ABOX_HAL_NO_ACK;reset_trace();
    assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_NO_ACK && strchr(trace,'T'));
    address_error=ABOX_HAL_OK;assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_ERROR);
    assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
    address_error=ABOX_HAL_ARBITRATION_LOST;reset_trace();
    assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_ARBITRATION_LOST && !strchr(trace,'T'));
    address_error=ABOX_HAL_OK;assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
    address_error=ABOX_HAL_BUS_TIMEOUT;reset_trace();
    assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_BUS_TIMEOUT && !strcmp(trace,"AnSW"));
    address_error=ABOX_HAL_OK;assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
    address_error=ABOX_HAL_BUS_TIMEOUT;reset_trace();
    assert(ABoxI2c_Quick(1,0x2aU,0U,10U)==ABOX_HAL_BUS_TIMEOUT && !strcmp(trace,"anSW"));
    address_error=ABOX_HAL_OK;assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
    /* Bulk write plus repeated START/read; the RX DMA IRQ generates STOP. */
    reset_trace();assert(ABoxI2c_TransferDma(1,a7,tx,4,rx,5,20)==ABOX_HAL_OK);
    assert(rx[0]==0x90U && rx[4]==0x94U && !dma_owned[6] && !dma_owned[7]);
    assert(strchr(trace,'W') && strchr(trace,'R') && strchr(trace,'T') && !primask);
    reset_trace();assert(ABoxI2c_TransferDma(1,a7,0,0,rx,2,20)==ABOX_HAL_OK);
    assert(strchr(trace,'N') && strchr(trace,'a') && rx[1]==0x91U);
    reset_trace();assert(ABoxI2c_TransferDma(1,a7,0,0,rx,1,20)==ABOX_HAL_OK && rx[0]==1U);
    dma_conflict=7U;reset_trace();assert(ABoxI2c_TransferDma(1,a7,tx,4,rx,5,20)==ABOX_HAL_BUSY);
    assert(!dma_owned[6] && !dma_owned[7] && !strchr(trace,'S'));dma_conflict=0U;
    dma_timeout=1U;reset_trace();now=0U;assert(ABoxI2c_TransferDma(1,a7,tx,4,0,0,3)==ABOX_HAL_TIMEOUT);
    assert(strchr(trace,'T') && !dma_owned[6]);dma_timeout=0U;
    assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
    dma_fail=1U;reset_trace();assert(ABoxI2c_TransferDma(1,a7,tx,4,0,0,20)==ABOX_HAL_ERROR);
    assert(strchr(trace,'T') && !dma_owned[6]);dma_fail=0U;
    assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
    fail_flag=ABOX_I2C_HW_RX_READY;reset_trace();
    assert(ABoxI2c_Transfer(1,a7,0,0,rx,1,2)==ABOX_HAL_TIMEOUT);
    assert(strchr(trace,'T') && !primask);
    fail_flag=255U;assert(ABoxI2c_Close(1)==ABOX_HAL_OK);
    {
        ABoxI2cSlaveConfig sc={{0x2aU,ABOX_I2C_ADDRESS_7BIT},1U,0x2bU,1U,1U};
        const ABoxI2cSlaveEvents se={(void *)1,slave_addressed,slave_read,slave_write,slave_ended,slave_failed,slave_alert};
        assert(ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
        assert(ABoxI2c_SlaveStart(1,&sc,&se)==ABOX_HAL_OK);
        assert(slave_hw[0].own1==0x4054U && slave_hw[0].own2==0x57U);
        assert((slave_hw[0].control2 & ABOX_I2C_CR2_IRQ)==ABOX_I2C_CR2_IRQ);
        assert((slave_hw[0].control1 & (ABOX_I2C_CR1_ACK|ABOX_I2C_CR1_GC))==(ABOX_I2C_CR1_ACK|ABOX_I2C_CR1_GC));
        assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_OK && ABoxI2c_Close(1)==ABOX_HAL_BUSY);
        assert((slave_hw[0].control2 & ABOX_I2C_CR2_IRQ)==ABOX_I2C_CR2_IRQ);
        assert(ABoxI2c_Quick(1U,0x2aU,0U,10U)==ABOX_HAL_OK);
        reset_trace();address_error=ABOX_HAL_ARBITRATION_LOST;
        assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_ARBITRATION_LOST && !strchr(trace,'T'));
        assert((slave_hw[0].control2 & ABOX_I2C_CR2_IRQ)==ABOX_I2C_CR2_IRQ);
        address_error=ABOX_HAL_NO_ACK;
        assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_NO_ACK);
        address_error=ABOX_HAL_OK;
        start_simulation=1U;now=0U;reset_trace();
        assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,30)==ABOX_HAL_OK);
        assert(slave_addresses==1U && slave_received==1U && slave_ends==1U && slave_end_reason==ABOX_I2C_SLAVE_STOP);
        slave_addresses=slave_received=slave_ends=0U;
        start_simulation=3U;now=0U;reset_trace();
        assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,30)==ABOX_HAL_OK);
        assert(slave_addresses==1U && slave_received==1U && slave_ends==1U && slave_end_reason==ABOX_I2C_SLAVE_STOP);
        slave_addresses=slave_received=slave_ends=0U;
        start_simulation=2U;now=0U;reset_trace();
        assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,3)==ABOX_HAL_BUSY && !strchr(trace,'T'));
        assert((slave_hw[0].control2 & ABOX_I2C_CR2_IRQ)==ABOX_I2C_CR2_IRQ);
        assert(!(slave_hw[0].control1 & ABOX_I2C_CR1_START));
        simulation_pending=start_simulation=0U;slave_hw[0].status1=0U;
        assert(ABoxI2c_TransferDma(1,a7,tx,1,rx,2,20)==ABOX_HAL_OK);
        {
            uint8_t count=0U, block[33];
            read_script[0]=1U;read_script[1]=0x55U;script_length=2U;script_position=0U;
            assert(ABoxI2c_TransferCounted(1U,0x2aU,tx,1U,block,32U,0U,&count,30U)==ABOX_HAL_OK);
            assert(count==1U && block[0]==0x55U);
            script_length=script_position=0U;
        }
        slave_hw[0].status1=0U;
        slave_hw[0].status1=ABOX_I2C_SR1_ADDR;slave_hw[0].status2=0U;
        assert(ABoxI2c_SlaveStop(1)==ABOX_HAL_BUSY);ABoxI2c_Irq(1U);
        slave_hw[0].status1=ABOX_I2C_SR1_SMBALERT;ABoxI2c_Irq(1U);assert(slave_alerts==1U);
        assert(slave_addresses==1U && slave_direction==ABOX_I2C_SLAVE_WRITE && slave_match==ABOX_I2C_SLAVE_PRIMARY);
        assert(ABoxI2c_SlaveStop(1)==ABOX_HAL_BUSY);
        slave_hw[0].status1=ABOX_I2C_SR1_RXNE|ABOX_I2C_SR1_BTF;ABoxI2c_Irq(1U);assert(slave_received==2U);
        slave_hw[0].status1=ABOX_I2C_SR1_ADDR;slave_hw[0].status2=ABOX_I2C_SR2_TRA|ABOX_I2C_SR2_DUAL;ABoxI2c_Irq(1U);
        assert(slave_ends==1U && slave_end_reason==ABOX_I2C_SLAVE_RESTART && slave_addresses==2U);
        assert(slave_direction==ABOX_I2C_SLAVE_READ && slave_match==ABOX_I2C_SLAVE_SECONDARY);
        reset_trace();slave_hw[0].status1=ABOX_I2C_SR1_TXE;ABoxI2c_Irq(1U);assert(strcmp(trace,"w")==0);
        slave_hw[0].status1=ABOX_I2C_SR1_AF|ABOX_I2C_SR1_STOP;ABoxI2c_Irq(1U);
        assert(slave_ends==2U && slave_end_reason==ABOX_I2C_SLAVE_NACK);
        slave_hw[0].status1=0U; /* fake registers do not implement write-zero-to-clear */
        assert(ABoxI2c_SlaveStop(1)==ABOX_HAL_OK && !(slave_hw[0].control2 & ABOX_I2C_CR2_IRQ));
        sc.own_address=a10;assert(ABoxI2c_SlaveStart(1,&sc,&se)==ABOX_HAL_INVALID);
        sc.dual_address=0U;sc.clock_stretch=0U;
        assert(ABoxI2c_SlaveStart(1,&sc,&se)==ABOX_HAL_OK && slave_hw[0].own1==0xc2abU);
        assert(slave_hw[0].control1 & ABOX_I2C_CR1_NOSTRETCH);
        slave_hw[0].status1=ABOX_I2C_SR1_ADDR;slave_hw[0].status2=ABOX_I2C_SR2_GC;ABoxI2c_Irq(1U);
        assert(slave_match==ABOX_I2C_SLAVE_GENERAL_CALL);
        slave_hw[0].status1=ABOX_I2C_SR1_BERR;ABoxI2c_Irq(1U);slave_hw[0].status1=0U;
        assert(slave_errors==1U && slave_error==ABOX_HAL_ERROR && ABoxI2c_SlaveStop(1)==ABOX_HAL_OK);
        assert(ABoxI2c_SlaveStart(1,&sc,&se)==ABOX_HAL_ERROR);
        assert(ABoxI2c_Close(1)==ABOX_HAL_OK && ABoxI2c_Open(1,&c)==ABOX_HAL_OK);
        assert(ABoxI2c_SlaveStart(1,&sc,&se)==ABOX_HAL_OK && ABoxI2c_SlaveAbort(1)==ABOX_HAL_OK);
        assert(ABoxI2c_Transfer(1,a7,tx,1,0,0,10)==ABOX_HAL_ERROR && ABoxI2c_Close(1)==ABOX_HAL_OK);
    }
    c.maximum_hz=400000U;c.fast_duty=ABOX_I2C_DUTY_16_9;
    assert(ABoxI2c_Open(2,&c)==ABOX_HAL_OK && configured_hz==360000U);
    assert(ABoxI2c_Close(2)==ABOX_HAL_OK);
    c.maximum_hz=100001U;
    assert(ABoxI2c_Open(2,&c)==ABOX_HAL_OK && configured_hz==100000U);
    assert(ABoxI2c_Close(2)==ABOX_HAL_OK);
    clock_hz=2000000U;c.maximum_hz=400000U;
    assert(ABoxI2c_Open(2,&c)==ABOX_HAL_OK && configured_hz==100000U);
    assert(ABoxI2c_Close(2)==ABOX_HAL_OK);
    return 0;
}
