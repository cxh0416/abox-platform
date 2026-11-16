#include <stdint.h>
#include "abox_i2c_backend.h"
#include "abox_i2c_counted.h"
#include "abox_i2c_f105_regs.h"
#include "abox_platform_port.h"
#include "abox_dma.h"

typedef struct {
    volatile uint8_t active, started, tx_done, rx_done;
    volatile ABoxHalStatus error;
    ABoxDmaEvents tx_events,rx_events;
    uint8_t tx_owned,rx_owned;
} I2cDma;
typedef struct {
    const ABoxI2cSlaveEvents *events;
    volatile uint8_t listening, active, transmit;
} I2cSlave;
enum { MASTER_IDLE, MASTER_WAIT_START, MASTER_ACTIVE };
typedef struct { uint8_t open; volatile uint8_t fault, master; I2cDma dma; I2cSlave slave; } I2cInstance;
static I2cInstance g_i2c[2];

static int valid(unsigned id) { return id >= 1U && id <= 2U; }
static int valid_address(ABoxI2cAddress address)
{
    return ((address.mode == ABOX_I2C_ADDRESS_7BIT && address.value <= 0x7fU) ||
            (address.mode == ABOX_I2C_ADDRESS_10BIT && address.value <= 0x3ffU));
}
static int tick_ready(void)
{
    const ABoxPlatformPort *p = ABox_PlatformPortGet();
    return p && p->get_tick_ms;
}
static int overlap(const uint8_t *a, uint16_t an, const uint8_t *b, uint16_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    return x <= y ? y - x < an : x - y < bn;
}
static ABoxHalStatus wait_flag(unsigned id, ABoxI2cHwFlag flag, int expected,
                               uint32_t start, uint32_t timeout)
{
    for (;;) {
        ABoxHalStatus error = ABoxI2cHw_Error(id);
        if (error != ABOX_HAL_OK) return error;
        if (ABoxI2cHw_Flag(id, flag) == expected) return ABOX_HAL_OK;
        if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout) return ABOX_HAL_TIMEOUT;
    }
}

/* A listening controller may be addressed before its requested START wins
 * the bus. Keep slave IRQs enabled until SB, then let the blocking master
 * path own the peripheral. Do not read SR2 while a slave ADDR can arrive. */
static void master_prepare(unsigned id)
{
    I2cInstance *s=&g_i2c[id-1U];
    if (s->slave.listening) {
        uint32_t key=ABoxI2cHw_EnterCritical();
        s->master=MASTER_WAIT_START;
        ABoxI2cHw_LeaveCritical(key);
    }
}

static ABoxHalStatus master_wait_start(unsigned id,volatile uint8_t *started,uint32_t start,uint32_t timeout)
{
    I2cInstance *s=&g_i2c[id-1U];
    ABoxI2cF105Registers *r=(ABoxI2cF105Registers *)ABoxI2cHw_Base(id);
    if (s->master!=MASTER_WAIT_START) return wait_flag(id,ABOX_I2C_HW_START,1,start,timeout);
    for (;;) {
        uint32_t key=ABoxI2cHw_EnterCritical();
        if ((r->status1 & ABOX_I2C_SR1_SB) && !s->slave.active) {
            r->control2 &= ~ABOX_I2C_CR2_IRQ;
            s->master=MASTER_ACTIVE;
            ABoxI2cHw_LeaveCritical(key);
            return ABOX_HAL_OK;
        }
        if (s->fault) {
            r->control1 &= ~ABOX_I2C_CR1_START;
            *started=0U;
            ABoxI2cHw_LeaveCritical(key);
            return ABOX_HAL_ERROR;
        }
        ABoxI2cHw_LeaveCritical(key);
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout) {
            key=ABoxI2cHw_EnterCritical();
            r->control1 &= ~ABOX_I2C_CR1_START;
            if (r->status1 & ABOX_I2C_SR1_SB) {
                r->control2 &= ~ABOX_I2C_CR2_IRQ;
                s->master=MASTER_ACTIVE;
                ABoxI2cHw_LeaveCritical(key);
                return ABOX_HAL_TIMEOUT;
            }
            *started=0U;
            ABoxI2cHw_LeaveCritical(key);
            return ABOX_HAL_BUSY;
        }
    }
}

static ABoxHalStatus master_release(unsigned id,ABoxHalStatus status,uint32_t start,uint32_t timeout)
{
    I2cInstance *s=&g_i2c[id-1U];
    ABoxI2cF105Registers *r;
    uint32_t key;
    if (s->master==MASTER_IDLE) return status;
    r=(ABoxI2cF105Registers *)ABoxI2cHw_Base(id);
    if (s->master==MASTER_ACTIVE && status==ABOX_HAL_NO_ACK &&
        wait_flag(id,ABOX_I2C_HW_STOP_PENDING,0,start,timeout)!=ABOX_HAL_OK) s->fault=1U;
    if (s->master==MASTER_ACTIVE && status!=ABOX_HAL_OK && status!=ABOX_HAL_ARBITRATION_LOST &&
        status!=ABOX_HAL_NO_ACK) s->fault=1U;
    key=ABoxI2cHw_EnterCritical();
    if (s->fault) r->control2 &= ~ABOX_I2C_CR2_IRQ;
    else if (s->master==MASTER_ACTIVE) {
        ABoxI2cHw_Ack(id,1);ABoxI2cHw_AckNext(id,0);
        r->control2 |= ABOX_I2C_CR2_IRQ;
    }
    s->master=MASTER_IDLE;
    ABoxI2cHw_LeaveCritical(key);
    return status;
}

/* Returns with the final ADDR flag set; the caller clears it at the point
 * required by the receive length or DMA setup. A 10-bit read first selects
 * the address in write direction unless this transaction already selected it. */
static ABoxHalStatus address_phase(unsigned id, ABoxI2cAddress address, int read,
                                   int selected, int ack_next, volatile uint8_t *started,
                                   uint32_t start, uint32_t timeout)
{
    ABoxHalStatus status;
    uint8_t header = (uint8_t)(0xf0U | ((address.value >> 7U) & 0x06U));
    ABoxI2cHw_Start(id); *started = 1U;
    status = master_wait_start(id, started, start, timeout);
    if (status != ABOX_HAL_OK) return status;
    if (ack_next) ABoxI2cHw_AckNext(id,1);
    if (address.mode == ABOX_I2C_ADDRESS_7BIT) {
        ABoxI2cHw_Address(id, (uint8_t)address.value, read);
    } else if (!read || !selected) {
        ABoxI2cHw_AddressByte(id, header);
        status = wait_flag(id, ABOX_I2C_HW_HEADER10, 1, start, timeout);
        if (status != ABOX_HAL_OK) return status;
        ABoxI2cHw_AddressByte(id, (uint8_t)address.value);
        status = wait_flag(id, ABOX_I2C_HW_ADDRESS, 1, start, timeout);
        if (status != ABOX_HAL_OK || !read) return status;
        ABoxI2cHw_ClearAddress(id);
        ABoxI2cHw_Start(id);
        status = wait_flag(id, ABOX_I2C_HW_START, 1, start, timeout);
        if (status != ABOX_HAL_OK) return status;
        ABoxI2cHw_AddressByte(id, (uint8_t)(header | 1U));
    } else ABoxI2cHw_AddressByte(id, (uint8_t)(header | 1U));
    return wait_flag(id, ABOX_I2C_HW_ADDRESS, 1, start, timeout);
}

ABoxHalStatus ABoxI2c_Open(unsigned id, const ABoxI2cConfig *config)
{
    uint32_t pclk, factor, divisor, speed, requested;
    uint64_t target;
    ABoxHalStatus status;
    if (!valid(id) || !config || !tick_ready() || !config->maximum_hz || config->maximum_hz > 400000U ||
        (unsigned)config->fast_duty > ABOX_I2C_DUTY_16_9) return ABOX_HAL_INVALID;
    if (g_i2c[id - 1U].open) return ABOX_HAL_BUSY;
    pclk = ABoxI2cHw_Pclk();
    if (pclk < 2000000U || pclk > ABoxI2cHw_MaxPclk()) return ABOX_HAL_UNSUPPORTED;
    factor = 2U;
    requested = config->maximum_hz > 100000U ? 100000U : config->maximum_hz;
    if (config->maximum_hz > 100000U && pclk >= 4000000U) {
        uint32_t fast_factor = config->fast_duty == ABOX_I2C_DUTY_2 ? 3U : 25U;
        target = (uint64_t)fast_factor * config->maximum_hz;
        divisor = (uint32_t)(((uint64_t)pclk + target - 1U) / target);
        /* If fast mode rounds down to 100 kHz or below, the standard-mode
         * timing registers must be used instead of rejecting a valid limit. */
        if (divisor && divisor <= 4095U && pclk / (fast_factor * divisor) > 100000U) {
            factor = fast_factor;
            requested = config->maximum_hz;
        }
    }
    target = (uint64_t)factor * requested;
    divisor = (uint32_t)(((uint64_t)pclk + target - 1U) / target);
    if (factor == 2U && divisor < 4U) divisor = 4U;
    if (!divisor || divisor > 4095U) return ABOX_HAL_UNSUPPORTED;
    speed = pclk / (factor * divisor);
    if (!speed) return ABOX_HAL_UNSUPPORTED;
    status = ABoxI2cHw_Open(id, speed, config->fast_duty);
    if (status != ABOX_HAL_OK) return status;
    g_i2c[id - 1U].open = 1U; g_i2c[id - 1U].fault = 0U;
    return ABOX_HAL_OK;
}
static unsigned dma_tx_channel(unsigned id) { return id==1U ? 6U : 4U; }
static unsigned dma_rx_channel(unsigned id) { return id==1U ? 7U : 5U; }
ABoxHalStatus ABoxI2c_SetSmbusMode(unsigned id,uint8_t enabled)
{
    I2cInstance *s;ABoxI2cF105Registers *r;uint32_t key;
    if (!valid(id) || enabled>1U) return ABOX_HAL_INVALID;
    s=&g_i2c[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->fault) return ABOX_HAL_ERROR;
    if (s->slave.listening || s->dma.active || s->dma.tx_owned || s->dma.rx_owned ||
        ABoxI2cHw_Flag(id,ABOX_I2C_HW_BUSY) || ABoxI2cHw_Flag(id,ABOX_I2C_HW_STOP_PENDING)) return ABOX_HAL_BUSY;
    r=(ABoxI2cF105Registers *)ABoxI2cHw_Base(id);if (!r) return ABOX_HAL_ERROR;
    key=ABoxI2cHw_EnterCritical();
    if (enabled) r->control1 |= ABOX_I2C_CR1_SMBUS|ABOX_I2C_CR1_SMBTYPE;
    else r->control1 &= ~(ABOX_I2C_CR1_SMBUS|ABOX_I2C_CR1_SMBTYPE);
    ABoxI2cHw_LeaveCritical(key);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2c_SmbusAlert(unsigned id,uint8_t clear,uint8_t *pending)
{
    I2cInstance *s;ABoxI2cF105Registers *r;uint32_t key;
    if (!valid(id) || clear>1U || !pending) return ABOX_HAL_INVALID;
    s=&g_i2c[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->slave.listening) return ABOX_HAL_BUSY;
    r=(ABoxI2cF105Registers *)ABoxI2cHw_Base(id);
    if (!r || !(r->control1 & ABOX_I2C_CR1_SMBUS)) return ABOX_HAL_INVALID;
    key=ABoxI2cHw_EnterCritical();*pending=(uint8_t)!!(r->status1 & ABOX_I2C_SR1_SMBALERT);
    if (clear && *pending) r->status1=~ABOX_I2C_SR1_SMBALERT;
    ABoxI2cHw_LeaveCritical(key);return ABOX_HAL_OK;
}
static ABoxHalStatus dma_cleanup(unsigned id)
{
    I2cDma *d=&g_i2c[id-1U].dma;ABoxHalStatus status;uint32_t key;
    key=ABoxI2cHw_EnterCritical();ABoxI2cHw_DmaRequest(id,0,0);d->active=0U;ABoxI2cHw_LeaveCritical(key);
    if (d->tx_owned) {
        status=ABoxDma_Abort(1U,dma_tx_channel(id));if(status!=ABOX_HAL_OK)return status;
        status=ABoxDma_Close(1U,dma_tx_channel(id));if(status!=ABOX_HAL_OK)return status;d->tx_owned=0U;
    }
    if (d->rx_owned) {
        status=ABoxDma_Abort(1U,dma_rx_channel(id));if(status!=ABOX_HAL_OK)return status;
        status=ABoxDma_Close(1U,dma_rx_channel(id));if(status!=ABOX_HAL_OK)return status;d->rx_owned=0U;
    }
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2c_Close(unsigned id)
{
    ABoxHalStatus status;
    if (!valid(id) || !g_i2c[id - 1U].open) return ABOX_HAL_INVALID;
    if (g_i2c[id - 1U].slave.listening) return ABOX_HAL_BUSY;
    if (g_i2c[id-1U].dma.tx_owned || g_i2c[id-1U].dma.rx_owned) {
        status=dma_cleanup(id);if(status!=ABOX_HAL_OK)return status;
    }
    status = ABoxI2cHw_Close(id);
    if (status != ABOX_HAL_OK) return status;
    g_i2c[id - 1U].open = 0U; g_i2c[id - 1U].fault = 0U;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2c_Quick(unsigned id,uint8_t address,uint8_t read_direction,uint32_t timeout_ms)
{
    ABoxI2cAddress target={address,ABOX_I2C_ADDRESS_7BIT};
    volatile uint8_t started=0U;uint32_t start,key;ABoxHalStatus status;
    if (!valid(id) || !tick_ready() || address>0x7fU || read_direction>1U || !g_i2c[id-1U].open)
        return ABOX_HAL_INVALID;
    if (g_i2c[id-1U].dma.active ||
        g_i2c[id-1U].dma.tx_owned || g_i2c[id-1U].dma.rx_owned) return ABOX_HAL_BUSY;
    if (g_i2c[id-1U].fault) return ABOX_HAL_ERROR;
    start=ABox_PortGetTickMs();master_prepare(id);
    if (!g_i2c[id-1U].slave.listening) {
        status=wait_flag(id,ABOX_I2C_HW_BUSY,0,start,timeout_ms);
        if (status==ABOX_HAL_TIMEOUT) return ABOX_HAL_BUSY;
        if (status!=ABOX_HAL_OK) goto failed_quick;
        ABoxI2cHw_Ack(id,0);ABoxI2cHw_AckNext(id,0);
    }
    status=address_phase(id,target,read_direction,0,0,&started,start,timeout_ms);
    if (status!=ABOX_HAL_OK) goto failed_quick;
    key=ABoxI2cHw_EnterCritical();ABoxI2cHw_ClearAddress(id);ABoxI2cHw_Stop(id);started=0U;
    ABoxI2cHw_LeaveCritical(key);
    status=wait_flag(id,ABOX_I2C_HW_STOP_PENDING,0,start,timeout_ms);
    if (status!=ABOX_HAL_OK) goto failed_quick;
    ABoxI2cHw_Ack(id,1);return master_release(id,ABOX_HAL_OK,start,timeout_ms);
failed_quick:
    if (status==ABOX_HAL_BUS_TIMEOUT) { g_i2c[id-1U].fault=1U;return master_release(id,status,start,timeout_ms); }
    if (started && status!=ABOX_HAL_ARBITRATION_LOST) ABoxI2cHw_Stop(id);
    if (g_i2c[id-1U].master!=MASTER_WAIT_START) { ABoxI2cHw_Ack(id,1);ABoxI2cHw_AckNext(id,0); }
    if (!g_i2c[id-1U].slave.listening) g_i2c[id-1U].fault=1U;
    return master_release(id,status,start,timeout_ms);
}

ABoxHalStatus ABoxI2c_TransferCounted(unsigned id,uint8_t address,const uint8_t *write,uint8_t write_length,
                                       uint8_t *read,uint8_t capacity,uint8_t trailing,uint8_t *count,uint32_t timeout_ms)
{
    ABoxI2cAddress target={address,ABOX_I2C_ADDRESS_7BIT};
    ABoxI2cF105Registers *r;volatile uint8_t started=0U;
    uint32_t start,key;uint8_t n,remaining,*out=read;ABoxHalStatus status;
    if (!valid(id) || !tick_ready() || address>0x7fU || !write || !write_length || !read ||
        !count || trailing>1U || capacity<1U || capacity>32U || !g_i2c[id-1U].open) return ABOX_HAL_INVALID;
    if (g_i2c[id-1U].dma.active ||
        g_i2c[id-1U].dma.tx_owned || g_i2c[id-1U].dma.rx_owned) return ABOX_HAL_BUSY;
    if (g_i2c[id-1U].fault) return ABOX_HAL_ERROR;
    r=(ABoxI2cF105Registers *)ABoxI2cHw_Base(id);if (!r) return ABOX_HAL_ERROR;
    start=ABox_PortGetTickMs();master_prepare(id);
    if (!g_i2c[id-1U].slave.listening) {
        status=wait_flag(id,ABOX_I2C_HW_BUSY,0,start,timeout_ms);
        if (status==ABOX_HAL_TIMEOUT) return ABOX_HAL_BUSY;
        if (status!=ABOX_HAL_OK) goto fail_counted;
        ABoxI2cHw_Ack(id,1);ABoxI2cHw_AckNext(id,0);
    }
    status=address_phase(id,target,0,0,0,&started,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
    ABoxI2cHw_ClearAddress(id);
    for (unsigned i=0U;i<write_length;++i) {
        status=wait_flag(id,ABOX_I2C_HW_TX_EMPTY,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
        ABoxI2cHw_Write(id,write[i]);
    }
    status=wait_flag(id,ABOX_I2C_HW_FINISHED,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
    status=address_phase(id,target,1,1,0,&started,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
    ABoxI2cHw_ClearAddress(id);
    status=wait_flag(id,ABOX_I2C_HW_RX_READY,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
    /* The count must be consumed before the following byte has completed.
     * A late service is rejected rather than returning a corrupted frame. */
    key=ABoxI2cHw_EnterCritical();
    if (r->status1 & ABOX_I2C_SR1_BTF) {
        ABoxI2cHw_LeaveCritical(key);status=ABOX_HAL_ERROR;goto fail_counted;
    }
    n=ABoxI2cHw_Read(id);
    if (!n || n>32U || n>capacity) {
        ABoxI2cHw_Ack(id,0);ABoxI2cHw_Stop(id);started=0U;
        ABoxI2cHw_LeaveCritical(key);status=ABOX_HAL_ERROR;goto fail_counted;
    }
    remaining=(uint8_t)(n+trailing);
    if (remaining==1U) { ABoxI2cHw_Ack(id,0);ABoxI2cHw_Stop(id);started=0U; }
    else if (remaining==2U) { ABoxI2cHw_AckNext(id,1);ABoxI2cHw_Ack(id,0); }
    ABoxI2cHw_LeaveCritical(key);
    if (remaining==1U) {
        status=wait_flag(id,ABOX_I2C_HW_RX_READY,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
        *out=ABoxI2cHw_Read(id);
    } else if (remaining==2U) {
        status=wait_flag(id,ABOX_I2C_HW_FINISHED,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
        key=ABoxI2cHw_EnterCritical();ABoxI2cHw_Stop(id);started=0U;*out++=ABoxI2cHw_Read(id);
        ABoxI2cHw_LeaveCritical(key);*out=ABoxI2cHw_Read(id);
    } else {
        while (remaining>3U) {
            status=wait_flag(id,ABOX_I2C_HW_RX_READY,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
            *out++=ABoxI2cHw_Read(id);--remaining;
        }
        status=wait_flag(id,ABOX_I2C_HW_FINISHED,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
        ABoxI2cHw_Ack(id,0);*out++=ABoxI2cHw_Read(id);
        status=wait_flag(id,ABOX_I2C_HW_FINISHED,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
        key=ABoxI2cHw_EnterCritical();ABoxI2cHw_Stop(id);started=0U;*out++=ABoxI2cHw_Read(id);
        ABoxI2cHw_LeaveCritical(key);*out=ABoxI2cHw_Read(id);
    }
    status=wait_flag(id,ABOX_I2C_HW_STOP_PENDING,0,start,timeout_ms);if(status!=ABOX_HAL_OK)goto fail_counted;
    ABoxI2cHw_Ack(id,1);ABoxI2cHw_AckNext(id,0);*count=n;return master_release(id,ABOX_HAL_OK,start,timeout_ms);
fail_counted:
    if (status!=ABOX_HAL_BUS_TIMEOUT) {
        if (started && status!=ABOX_HAL_ARBITRATION_LOST) ABoxI2cHw_Stop(id);
    }
    if (!g_i2c[id-1U].slave.listening || status==ABOX_HAL_BUS_TIMEOUT) g_i2c[id-1U].fault=1U;
    return master_release(id,status,start,timeout_ms);
}

ABoxHalStatus ABoxI2c_Transfer(unsigned id, ABoxI2cAddress address, const uint8_t *write,
                               uint16_t write_length, uint8_t *read, uint16_t read_length,
                               uint32_t timeout_ms)
{
    uint32_t start, key;
    uint16_t remaining = read_length;
    volatile uint8_t started = 0U;
    ABoxHalStatus status;
    if (!valid(id) || !tick_ready() || !valid_address(address) || (!write_length && !read_length) ||
        (write_length && !write) || (read_length && !read) || overlap(write, write_length, read, read_length))
        return ABOX_HAL_INVALID;
    if (!g_i2c[id - 1U].open) return ABOX_HAL_INVALID;
    if (g_i2c[id - 1U].dma.active || g_i2c[id - 1U].dma.tx_owned || g_i2c[id - 1U].dma.rx_owned) return ABOX_HAL_BUSY;
    if (g_i2c[id - 1U].fault) return ABOX_HAL_ERROR;
    start = ABox_PortGetTickMs(); master_prepare(id);
    if (!g_i2c[id-1U].slave.listening) {
        status = wait_flag(id, ABOX_I2C_HW_BUSY, 0, start, timeout_ms);
        if (status == ABOX_HAL_TIMEOUT) return ABOX_HAL_BUSY;
        if (status != ABOX_HAL_OK) goto failed;
        ABoxI2cHw_Ack(id, 1); ABoxI2cHw_AckNext(id, 0);
    }
    if (write_length) {
        status = address_phase(id,address,0,0,0,&started,start,timeout_ms); if (status != ABOX_HAL_OK) goto failed;
        ABoxI2cHw_ClearAddress(id);
        for (uint16_t i = 0U; i < write_length; ++i) {
            status = wait_flag(id, ABOX_I2C_HW_TX_EMPTY, 1, start, timeout_ms); if (status != ABOX_HAL_OK) goto failed;
            ABoxI2cHw_Write(id, write[i]);
        }
        status = wait_flag(id, ABOX_I2C_HW_FINISHED, 1, start, timeout_ms); if (status != ABOX_HAL_OK) goto failed;
    }
    if (read_length) {
        status = address_phase(id,address,1,write_length != 0U,read_length == 2U,&started,start,timeout_ms); if (status != ABOX_HAL_OK) goto failed;
        if (remaining == 1U) {
            ABoxI2cHw_Ack(id, 0);
            key = ABoxI2cHw_EnterCritical();
            ABoxI2cHw_ClearAddress(id); ABoxI2cHw_Stop(id); started = 0;
            ABoxI2cHw_LeaveCritical(key);
            status = wait_flag(id, ABOX_I2C_HW_RX_READY, 1, start, timeout_ms); if (status != ABOX_HAL_OK) goto failed;
            *read = ABoxI2cHw_Read(id);
        } else if (remaining == 2U) {
            key = ABoxI2cHw_EnterCritical();
            ABoxI2cHw_ClearAddress(id); ABoxI2cHw_Ack(id, 0);
            ABoxI2cHw_LeaveCritical(key);
            status = wait_flag(id, ABOX_I2C_HW_FINISHED, 1, start, timeout_ms); if (status != ABOX_HAL_OK) goto failed;
            key = ABoxI2cHw_EnterCritical();
            ABoxI2cHw_Stop(id); started = 0; *read++ = ABoxI2cHw_Read(id);
            ABoxI2cHw_LeaveCritical(key);
            *read = ABoxI2cHw_Read(id);
        } else {
            ABoxI2cHw_ClearAddress(id);
            while (remaining > 3U) {
                status = wait_flag(id, ABOX_I2C_HW_RX_READY, 1, start, timeout_ms); if (status != ABOX_HAL_OK) goto failed;
                *read++ = ABoxI2cHw_Read(id); --remaining;
            }
            status = wait_flag(id, ABOX_I2C_HW_FINISHED, 1, start, timeout_ms); if (status != ABOX_HAL_OK) goto failed;
            ABoxI2cHw_Ack(id, 0);
            *read++ = ABoxI2cHw_Read(id);
            status = wait_flag(id, ABOX_I2C_HW_FINISHED, 1, start, timeout_ms); if (status != ABOX_HAL_OK) goto failed;
            key = ABoxI2cHw_EnterCritical();
            ABoxI2cHw_Stop(id); started = 0; *read++ = ABoxI2cHw_Read(id);
            ABoxI2cHw_LeaveCritical(key);
            *read = ABoxI2cHw_Read(id);
        }
    } else {
        ABoxI2cHw_Stop(id); started = 0;
    }
    status = wait_flag(id, ABOX_I2C_HW_STOP_PENDING, 0, start, timeout_ms);
    if (status != ABOX_HAL_OK) goto failed;
    ABoxI2cHw_Ack(id, 1); ABoxI2cHw_AckNext(id, 0);
    return master_release(id,ABOX_HAL_OK,start,timeout_ms);
failed:
    /* In SMBus mode the peripheral requests STOP on a hardware timeout.
     * Do not rewrite CR1 while STOP is pending; Close/Open recovers it. */
    if (status == ABOX_HAL_BUS_TIMEOUT) { g_i2c[id-1U].fault=1U;return master_release(id,status,start,timeout_ms); }
    if (started && status != ABOX_HAL_ARBITRATION_LOST) ABoxI2cHw_Stop(id);
    if (g_i2c[id-1U].master!=MASTER_WAIT_START) { ABoxI2cHw_Ack(id, 1); ABoxI2cHw_AckNext(id, 0); }
    if (!g_i2c[id-1U].slave.listening) g_i2c[id - 1U].fault = 1U;
    return master_release(id,status,start,timeout_ms);
}

static void dma_event(void *context,unsigned controller,unsigned channel,ABoxDmaEvent event)
{
    I2cDma *d=(I2cDma *)context;unsigned id=d==&g_i2c[0].dma ? 1U : 2U;
    (void)controller;
    if (!d->active || event==ABOX_DMA_HALF) return;
    if (event==ABOX_DMA_TRANSFER_ERROR) { d->error=ABOX_HAL_ERROR;ABoxI2cHw_DmaRequest(id,0,0);return; }
    if (channel==dma_rx_channel(id)) {
        ABoxHalStatus error=ABoxI2cHw_Error(id);
        if (error!=ABOX_HAL_OK) d->error=error;
        if (error!=ABOX_HAL_ARBITRATION_LOST && error!=ABOX_HAL_BUS_TIMEOUT) ABoxI2cHw_Stop(id);
        d->started=0U;ABoxI2cHw_DmaRequest(id,0,0);d->rx_done=1U;
    } else if (channel==dma_tx_channel(id)) {
        ABoxI2cHw_DmaRequest(id,0,0);d->tx_done=1U;
    }
}
static ABoxHalStatus wait_dma(unsigned id,volatile uint8_t *done,uint32_t start,uint32_t timeout)
{
    I2cDma *d=&g_i2c[id-1U].dma;
    for (;;) {
        uint32_t key=ABoxI2cHw_EnterCritical();
        ABoxHalStatus status=ABoxI2cHw_Error(id);
        if(status!=ABOX_HAL_OK) {
            d->active=0U;ABoxI2cHw_DmaRequest(id,0,0);
            if(status==ABOX_HAL_ARBITRATION_LOST || status==ABOX_HAL_BUS_TIMEOUT)d->started=0U;
        } else if(d->error!=ABOX_HAL_OK) status=d->error;
        if(status!=ABOX_HAL_OK) { ABoxI2cHw_LeaveCritical(key);return status; }
        if(*done) { ABoxI2cHw_LeaveCritical(key);return ABOX_HAL_OK; }
        ABoxI2cHw_LeaveCritical(key);
        if((uint32_t)(ABox_PortGetTickMs()-start)>=timeout)return ABOX_HAL_TIMEOUT;
    }
}
ABoxHalStatus ABoxI2c_TransferDma(unsigned id,ABoxI2cAddress address,const uint8_t *write,uint16_t write_length,
                                  uint8_t *read,uint16_t read_length,uint32_t timeout_ms)
{
    I2cDma *d;ABoxHalStatus status,cleanup_status;uint32_t start,key;
    ABoxDmaConfig c={ABOX_DMA_MEMORY_TO_PERIPHERAL,1U,1U,0U,1U,0U,1U};
    if (read_length==1U) return ABoxI2c_Transfer(id,address,write,write_length,read,read_length,timeout_ms);
    if (!valid(id) || !tick_ready() || !valid_address(address) || (!write_length && !read_length) ||
        (write_length && !write) || (read_length && !read) || overlap(write,write_length,read,read_length)) return ABOX_HAL_INVALID;
    if (!g_i2c[id-1U].open) return ABOX_HAL_INVALID;
    d=&g_i2c[id-1U].dma;
    if (d->active || d->tx_owned || d->rx_owned) return ABOX_HAL_BUSY;
    if (g_i2c[id-1U].fault) return ABOX_HAL_ERROR;
    d->active=1U;d->started=0U;d->tx_done=0U;d->rx_done=0U;d->error=ABOX_HAL_OK;
    d->tx_events.context=d;d->tx_events.on_event=dma_event;d->rx_events.context=d;d->rx_events.on_event=dma_event;
    start=ABox_PortGetTickMs();master_prepare(id);
    if (write_length) {
        status=ABoxDma_Open(1U,dma_tx_channel(id),&c);if(status!=ABOX_HAL_OK)goto done;
        d->tx_owned=1U;
    }
    if (read_length) {
        c.direction=ABOX_DMA_PERIPHERAL_TO_MEMORY;
        status=ABoxDma_Open(1U,dma_rx_channel(id),&c);if(status!=ABOX_HAL_OK)goto done;
        d->rx_owned=1U;
    }
    if (!g_i2c[id-1U].slave.listening) {
        status=wait_flag(id,ABOX_I2C_HW_BUSY,0,start,timeout_ms);
        if(status==ABOX_HAL_TIMEOUT){status=ABOX_HAL_BUSY;goto done;}
        if(status!=ABOX_HAL_OK)goto failed;
        ABoxI2cHw_Ack(id,1);ABoxI2cHw_AckNext(id,0);
    }
    if(write_length) {
        status=address_phase(id,address,0,0,0,&d->started,start,timeout_ms);if(status!=ABOX_HAL_OK)goto failed;
        status=ABoxDma_Start(1U,dma_tx_channel(id),(uintptr_t)write,ABoxI2cHw_DmaAddress(id),write_length,&d->tx_events);
        if(status!=ABOX_HAL_OK)goto failed;
        ABoxI2cHw_DmaRequest(id,1,0);ABoxI2cHw_ClearAddress(id);
        status=wait_dma(id,&d->tx_done,start,timeout_ms);if(status!=ABOX_HAL_OK)goto failed;
        status=wait_flag(id,ABOX_I2C_HW_FINISHED,1,start,timeout_ms);if(status!=ABOX_HAL_OK)goto failed;
    }
    if(read_length) {
        status=address_phase(id,address,1,write_length!=0U,read_length==2U,&d->started,start,timeout_ms);if(status!=ABOX_HAL_OK)goto failed;
        status=ABoxDma_Start(1U,dma_rx_channel(id),ABoxI2cHw_DmaAddress(id),(uintptr_t)read,read_length,&d->rx_events);
        if(status!=ABOX_HAL_OK)goto failed;
        key=ABoxI2cHw_EnterCritical();
        ABoxI2cHw_DmaRequest(id,1,1);ABoxI2cHw_ClearAddress(id);
        if(read_length==2U)ABoxI2cHw_Ack(id,0);
        ABoxI2cHw_LeaveCritical(key);
        status=wait_dma(id,&d->rx_done,start,timeout_ms);if(status!=ABOX_HAL_OK)goto failed;
    } else { ABoxI2cHw_Stop(id);d->started=0U; }
    status=wait_flag(id,ABOX_I2C_HW_STOP_PENDING,0,start,timeout_ms);if(status!=ABOX_HAL_OK)goto failed;
    ABoxI2cHw_Ack(id,1);ABoxI2cHw_AckNext(id,0);goto done;
failed:
    key=ABoxI2cHw_EnterCritical();ABoxI2cHw_DmaRequest(id,0,0);
    if(d->started && status!=ABOX_HAL_ARBITRATION_LOST && status!=ABOX_HAL_BUS_TIMEOUT)ABoxI2cHw_Stop(id);
    d->started=0U;d->active=0U;ABoxI2cHw_LeaveCritical(key);
    if(status!=ABOX_HAL_BUS_TIMEOUT && g_i2c[id-1U].master!=MASTER_WAIT_START) { ABoxI2cHw_Ack(id,1);ABoxI2cHw_AckNext(id,0); }
    if (!g_i2c[id-1U].slave.listening || status==ABOX_HAL_BUS_TIMEOUT) g_i2c[id-1U].fault=1U;
done:
    cleanup_status=dma_cleanup(id);
    if(cleanup_status!=ABOX_HAL_OK){g_i2c[id-1U].fault=1U;return master_release(id,cleanup_status,start,timeout_ms);}
    return master_release(id,status,start,timeout_ms);
}

static ABoxI2cF105Registers *slave_regs(unsigned id)
{ return (ABoxI2cF105Registers *)ABoxI2cHw_Base(id); }
static void slave_end(I2cSlave *s,unsigned id,ABoxI2cSlaveEnd reason)
{
    const ABoxI2cSlaveEvents *e=s->events;
    s->active=0U;
    if(e && e->ended)e->ended(e->context,id,reason);
}
ABoxHalStatus ABoxI2c_SlaveStart(unsigned id,const ABoxI2cSlaveConfig *config,
                                  const ABoxI2cSlaveEvents *events)
{
    I2cInstance *instance; I2cSlave *s; ABoxI2cF105Registers *r;
    if(!valid(id) || !config || !valid_address(config->own_address) ||
       config->dual_address>1U || config->general_call>1U || config->clock_stretch>1U ||
       (config->dual_address && (config->own_address.mode!=ABOX_I2C_ADDRESS_7BIT || config->second_address>0x7fU)))
        return ABOX_HAL_INVALID;
    instance=&g_i2c[id-1U];s=&instance->slave;
    if(!instance->open)return ABOX_HAL_INVALID;
    if(instance->fault)return ABOX_HAL_ERROR;
    if(s->listening || instance->dma.active || instance->dma.tx_owned || instance->dma.rx_owned)return ABOX_HAL_BUSY;
    r=slave_regs(id);if(!r)return ABOX_HAL_ERROR;
    r->control1 &= ~ABOX_I2C_CR1_PE;
    r->own1=ABoxI2cHw_OwnAddress(config->own_address);
    r->own2=config->dual_address ? ((uint32_t)config->second_address<<1U)|1U : 0U;
    r->control1 &= ~(ABOX_I2C_CR1_GC|ABOX_I2C_CR1_NOSTRETCH);
    if(config->general_call)r->control1 |= ABOX_I2C_CR1_GC;
    if(!config->clock_stretch)r->control1 |= ABOX_I2C_CR1_NOSTRETCH;
    s->events=events;s->active=0U;s->transmit=0U;s->listening=1U;
    r->control1 |= ABOX_I2C_CR1_PE|ABOX_I2C_CR1_ACK;
    r->control2 |= ABOX_I2C_CR2_IRQ;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2c_SlaveStop(unsigned id)
{
    I2cSlave *s; ABoxI2cF105Registers *r;
    if(!valid(id) || !g_i2c[id-1U].open)return ABOX_HAL_INVALID;
    s=&g_i2c[id-1U].slave;if(!s->listening)return ABOX_HAL_INVALID;
    r=slave_regs(id);
    if(s->active || (r->status1 & ABOX_I2C_SR1_ADDR))return ABOX_HAL_BUSY;
    r->control2 &= ~ABOX_I2C_CR2_IRQ;
    if (!g_i2c[id-1U].fault)
        r->control1 &= ~(ABOX_I2C_CR1_ACK|ABOX_I2C_CR1_GC|ABOX_I2C_CR1_NOSTRETCH);
    s->listening=0U;s->events=0;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2c_SlaveAbort(unsigned id)
{
    I2cSlave *s; ABoxI2cF105Registers *r;
    if(!valid(id) || !g_i2c[id-1U].open)return ABOX_HAL_INVALID;
    s=&g_i2c[id-1U].slave;if(!s->listening)return ABOX_HAL_INVALID;
    r=slave_regs(id);r->control2 &= ~ABOX_I2C_CR2_IRQ;r->control1 &= ~ABOX_I2C_CR1_PE;
    s->active=0U;s->listening=0U;s->events=0;g_i2c[id-1U].fault=1U;
    return ABOX_HAL_OK;
}
void ABoxI2c_Irq(unsigned id)
{
    I2cSlave *s; ABoxI2cF105Registers *r; const ABoxI2cSlaveEvents *e;
    uint32_t flags,errors,info; volatile uint32_t discard;
    if(!valid(id) || !g_i2c[id-1U].open)return;
    s=&g_i2c[id-1U].slave;if(!s->listening || g_i2c[id-1U].master==MASTER_ACTIVE)return;
    r=slave_regs(id);e=s->events;flags=r->status1;
    if (g_i2c[id-1U].master==MASTER_WAIT_START && (flags & ABOX_I2C_SR1_SB) && !s->active) {
        r->control2 &= ~ABOX_I2C_CR2_IRQ;
        return;
    }
    if (flags & ABOX_I2C_SR1_SMBALERT) {
        r->status1=~ABOX_I2C_SR1_SMBALERT;
        if (e && e->smbus_alert) e->smbus_alert(e->context,id);
    }
    errors=flags & (ABOX_I2C_SR1_BERR|ABOX_I2C_SR1_ARLO|ABOX_I2C_SR1_OVR|ABOX_I2C_SR1_TIMEOUT);
    if(errors) {
        r->status1=~errors;r->control2 &= ~ABOX_I2C_CR2_IRQ;
        if (!(errors & ABOX_I2C_SR1_TIMEOUT)) r->control1 &= ~ABOX_I2C_CR1_ACK;
        s->active=0U;g_i2c[id-1U].fault=1U;
        if(e && e->error)e->error(e->context,id,(errors & ABOX_I2C_SR1_ARLO) ? ABOX_HAL_ARBITRATION_LOST :
            (errors & ABOX_I2C_SR1_TIMEOUT) ? ABOX_HAL_BUS_TIMEOUT : ABOX_HAL_ERROR);
        return;
    }
    /* Drain both buffered bytes before STOP/repeated START if BTF is set. */
    if(s->active && !s->transmit && (flags & ABOX_I2C_SR1_RXNE)) {
        uint8_t byte=ABoxI2cHw_Read(id);
        uint8_t second=0U;
        if(flags & ABOX_I2C_SR1_BTF)second=ABoxI2cHw_Read(id);
        if(e && e->received)e->received(e->context,id,byte);
        if(flags & ABOX_I2C_SR1_BTF) {
            if(e && e->received)e->received(e->context,id,second);
        }
    }
    if(flags & ABOX_I2C_SR1_AF) {
        r->status1=~ABOX_I2C_SR1_AF;
        if(s->active && s->transmit)slave_end(s,id,ABOX_I2C_SLAVE_NACK);
        else {
            r->control2 &= ~ABOX_I2C_CR2_IRQ;r->control1 &= ~ABOX_I2C_CR1_ACK;
            s->active=0U;g_i2c[id-1U].fault=1U;
            if(e && e->error)e->error(e->context,id,ABOX_HAL_NO_ACK);
            return;
        }
    }
    if(flags & ABOX_I2C_SR1_STOP) {
        discard=r->status1; (void)discard; r->control1 |= ABOX_I2C_CR1_PE;
        if(s->active)slave_end(s,id,ABOX_I2C_SLAVE_STOP);
    }
    if(flags & ABOX_I2C_SR1_ADDR) {
        ABoxI2cSlaveMatch match;
        if(s->active)slave_end(s,id,ABOX_I2C_SLAVE_RESTART);
        discard=r->status1;info=r->status2;(void)discard;
        s->transmit=(info & ABOX_I2C_SR2_TRA) ? 1U : 0U;s->active=1U;
        match=(info & ABOX_I2C_SR2_GC) ? ABOX_I2C_SLAVE_GENERAL_CALL :
              (info & ABOX_I2C_SR2_DUAL) ? ABOX_I2C_SLAVE_SECONDARY : ABOX_I2C_SLAVE_PRIMARY;
        if(e && e->addressed)e->addressed(e->context,id,s->transmit ? ABOX_I2C_SLAVE_READ : ABOX_I2C_SLAVE_WRITE,match);
    }
    if(s->active && s->transmit && (flags & ABOX_I2C_SR1_TXE) && !(flags & ABOX_I2C_SR1_SB))
        ABoxI2cHw_Write(id,e && e->transmit ? e->transmit(e->context,id) : 0xffU);
    if (g_i2c[id-1U].master==MASTER_WAIT_START && (r->status1 & ABOX_I2C_SR1_SB)) {
        if (s->active) slave_end(s,id,ABOX_I2C_SLAVE_RESTART);
        r->control2 &= ~ABOX_I2C_CR2_IRQ;
    }
}
