#include <string.h>
#include <stddef.h>
#include "abox_spi_common.h"
#include "abox_spi_dma_backend.h"
#include "abox_i2s_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_spi.h"
#if defined(GD32F10X_CL)
_Static_assert(offsetof(ABoxI2sRegisters,data)==0x0cU && offsetof(ABoxI2sRegisters,i2s_config)==0x1cU &&
               offsetof(ABoxI2sRegisters,i2s_prescaler)==0x20U,"I2S register layout");
_Static_assert(SPI_I2SCTL_I2SSEL==(1U<<11) && SPI_I2SCTL_I2SEN==(1U<<10) &&
               SPI_I2SCTL_I2SOPMOD==(3U<<8) && SPI_I2SCTL_I2SSTD==(3U<<4) && SPI_I2SCTL_DTLEN==(3U<<1) &&
               SPI_STAT_TXURERR==(1U<<3) && SPI_STAT_RXORERR==(1U<<6),"I2S control bits");
_Static_assert(offsetof(ABoxI2sRegisters,crc_poly)==0x10U && SPI_CTL0_SPIEN==(1U<<6) &&
               SPI_CTL0_CRCNT==(1U<<12) && SPI_CTL0_CRCEN==(1U<<13) &&
               SPI_STAT_RBNE==1U && SPI_STAT_TBE==2U && SPI_STAT_CRCERR==(1U<<4),"SPI CRC layout");
_Static_assert(offsetof(ABoxI2sClockRegisters,config)==0x2cU && RCU_CTL_HXTALSTB==(1U<<17) &&
               RCU_CTL_PLL1EN==(1U<<26) && RCU_CTL_PLL2EN==(1U<<28) && RCU_CTL_PLL2STB==(1U<<29) &&
               RCU_CFG1_PREDV1==(15U<<4) && RCU_CFG1_PLL2MF==(15U<<12) &&
               RCU_CFG1_I2S1SEL==(1U<<17) && RCU_CFG1_I2S2SEL==(1U<<18),"I2S clock layout");
#endif

typedef struct { ABoxSpiConfig config; uint8_t open, fault, dma_busy, dma_directions; } SpiInstance;
static SpiInstance g_spi[3];

static uint32_t spi(unsigned id)
{
    switch (id) { case 1: return SPI0; case 2: return SPI1; case 3: return SPI2; default: return 0U; }
}
static void clock_enable(unsigned id)
{
    switch (id) { case 1: rcu_periph_clock_enable(RCU_SPI0); break;
                  case 2: rcu_periph_clock_enable(RCU_SPI1); break;
                  case 3: rcu_periph_clock_enable(RCU_SPI2); break; default: break; }
}
static void clock_disable(unsigned id)
{
    switch (id) { case 1: rcu_periph_clock_disable(RCU_SPI0); break;
                  case 2: rcu_periph_clock_disable(RCU_SPI1); break;
                  case 3: rcu_periph_clock_disable(RCU_SPI2); break; default: break; }
}

ABoxHalStatus ABoxSpi_Open(unsigned id, const ABoxSpiConfig *config)
{
    SpiInstance *s;
    spi_parameter_struct p;
    unsigned exponent = 0U;
    uint16_t divisor;
    uint32_t instance;
    if (!ABoxSpi_ConfigValid(id, config)) return ABOX_HAL_INVALID;
    s = &g_spi[id - 1U];
    if (s->open) return ABOX_HAL_BUSY;
    instance = spi(id);
    spi_struct_para_init(&p);
    p.device_mode = config->role == ABOX_SPI_MASTER ? SPI_MASTER : SPI_SLAVE;
    p.trans_mode = config->wiring == ABOX_SPI_TWO_LINE_RX ? SPI_TRANSMODE_RECEIVEONLY :
                   config->wiring == ABOX_SPI_ONE_LINE ? SPI_TRANSMODE_BDRECEIVE : SPI_TRANSMODE_FULLDUPLEX;
    p.frame_size = config->bits == 16U ? SPI_FRAMESIZE_16BIT : SPI_FRAMESIZE_8BIT;
    p.nss = config->nss == ABOX_SPI_NSS_SOFTWARE ? SPI_NSS_SOFT : SPI_NSS_HARD;
    p.endian = config->lsb_first ? SPI_ENDIAN_LSB : SPI_ENDIAN_MSB;
    p.clock_polarity_phase = config->cpol ?
        (config->cpha ? SPI_CK_PL_HIGH_PH_2EDGE : SPI_CK_PL_HIGH_PH_1EDGE) :
        (config->cpha ? SPI_CK_PL_LOW_PH_2EDGE : SPI_CK_PL_LOW_PH_1EDGE);
    for (divisor = config->prescaler; divisor > 2U; divisor >>= 1U) ++exponent;
    p.prescale = exponent << 3U;
    clock_enable(id);
    spi_i2s_deinit(instance);
    spi_init(instance, &p);
    if (config->nss == ABOX_SPI_NSS_OUTPUT) spi_nss_output_enable(instance);
    s->config = *config; s->fault = 0U; s->open = 1U;
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxSpi_Close(unsigned id)
{
    SpiInstance *s;
    uint32_t instance = spi(id);
    if (!instance) return ABOX_HAL_INVALID;
    s = &g_spi[id - 1U];
    if (s->open!=1U) return ABOX_HAL_INVALID;
    if (s->dma_busy) return ABOX_HAL_BUSY;
    spi_disable(instance);
    spi_i2s_deinit(instance);
    clock_disable(id);
    s->open = 0U; s->fault = 0U;
    return ABOX_HAL_OK;
}

static ABoxHalStatus wait_flag(uint32_t instance, uint32_t flag, FlagStatus expected,
                               uint32_t start, uint32_t timeout_ms)
{
    for (;;) {
        if (spi_i2s_flag_get(instance, SPI_FLAG_CONFERR) != RESET ||
            spi_i2s_flag_get(instance, SPI_FLAG_RXORERR) != RESET) return ABOX_HAL_ERROR;
        if (spi_i2s_flag_get(instance, flag) == expected) return ABOX_HAL_OK;
        if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout_ms) return ABOX_HAL_TIMEOUT;
    }
}

ABoxHalStatus ABoxSpi_Transfer(unsigned id, const void *tx, void *rx,
                               uint16_t frames, uint32_t timeout_ms)
{
    SpiInstance *s;
    uint32_t instance = spi(id), start;
    ABoxHalStatus result = ABOX_HAL_OK;
    const uint8_t *source = (const uint8_t *)tx;
    uint8_t *dest = (uint8_t *)rx;
    unsigned width;
    if (!instance) return ABOX_HAL_INVALID;
    s = &g_spi[id - 1U];
    if (s->open!=1U || !ABoxSpi_TransferValid(&s->config, tx, rx, frames) || !ABoxSpi_TickReady())
        return ABOX_HAL_INVALID;
    if (s->dma_busy) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    width = s->config.bits / 8U;
    start = ABox_PortGetTickMs();
    if (s->config.wiring == ABOX_SPI_ONE_LINE) {
        spi_disable(instance);
        spi_bidirectional_transfer_config(instance, source ? SPI_BIDIRECTIONAL_TRANSMIT : SPI_BIDIRECTIONAL_RECEIVE);
    }
    spi_enable(instance);
    for (unsigned i = 0U; i < frames; ++i) {
        uint16_t value = width == 1U ? 0xffU : 0xffffU;
        if (s->config.wiring == ABOX_SPI_TWO_LINE_FULL || source) {
            result = wait_flag(instance, SPI_FLAG_TBE, SET, start, timeout_ms);
            if (result != ABOX_HAL_OK) break;
            if (source) { value = 0U; memcpy(&value, source + i * width, width); }
            spi_i2s_data_transmit(instance, value);
        }
        if (s->config.wiring == ABOX_SPI_TWO_LINE_FULL || dest) {
            result = wait_flag(instance, SPI_FLAG_RBNE, SET, start, timeout_ms);
            if (result != ABOX_HAL_OK) break;
            if (s->config.wiring != ABOX_SPI_TWO_LINE_FULL && i+1U == frames) spi_disable(instance);
            value = spi_i2s_data_receive(instance);
            if (dest) memcpy(dest + i * width, &value, width);
        }
    }
    if (result == ABOX_HAL_OK && (source || s->config.wiring == ABOX_SPI_TWO_LINE_FULL))
        result = wait_flag(instance, SPI_FLAG_TBE, SET, start, timeout_ms);
    if (result == ABOX_HAL_OK) result = wait_flag(instance, SPI_FLAG_TRANS, RESET, start, timeout_ms);
    if (result != ABOX_HAL_OK) s->fault = 1U;
    return result;
}

ABoxHalStatus ABoxSpiHw_DmaReserve(unsigned id,const void *tx,void *rx,uint16_t frames,
                                   uint8_t *width,uint8_t *directions)
{
    SpiInstance *s;
    if (!spi(id) || !width || !directions) return ABOX_HAL_INVALID;
    s=&g_spi[id-1U];
    if (s->open!=1U || !ABoxSpi_TransferValid(&s->config,tx,rx,frames)) return ABOX_HAL_INVALID;
    if (s->dma_busy) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    s->dma_directions=s->config.wiring==ABOX_SPI_TWO_LINE_FULL ? ABOX_SPI_DMA_RX|ABOX_SPI_DMA_TX :
                      rx ? ABOX_SPI_DMA_RX : ABOX_SPI_DMA_TX;
    s->dma_busy=1U; *width=s->config.bits/8U; *directions=s->dma_directions; return ABOX_HAL_OK;
}
void ABoxSpiHw_DmaRelease(unsigned id,int fault)
{ SpiInstance *s=&g_spi[id-1U]; if (fault) s->fault=1U; s->dma_busy=0U; }
uintptr_t ABoxSpiHw_DmaAddress(unsigned id) { return (uintptr_t)&SPI_DATA(spi(id)); }
void ABoxSpiHw_DmaRequest(unsigned id,int enable)
{
    uint32_t p=spi(id); SpiInstance *s=&g_spi[id-1U];
    if (enable) {
        if (s->config.wiring==ABOX_SPI_ONE_LINE) {
            spi_disable(p);
            spi_bidirectional_transfer_config(p,(s->dma_directions & ABOX_SPI_DMA_TX) ?
                                               SPI_BIDIRECTIONAL_TRANSMIT : SPI_BIDIRECTIONAL_RECEIVE);
        }
        if (s->dma_directions & ABOX_SPI_DMA_RX) spi_dma_enable(p,SPI_DMA_RECEIVE);
        if (s->dma_directions & ABOX_SPI_DMA_TX) spi_dma_enable(p,SPI_DMA_TRANSMIT);
        spi_enable(p);
    }
    else { spi_dma_disable(p,SPI_DMA_RECEIVE); spi_dma_disable(p,SPI_DMA_TRANSMIT); }
}
void ABoxSpiHw_DmaHaltReceive(unsigned id)
{
    SpiInstance *s=&g_spi[id-1U];
    if (s->config.wiring!=ABOX_SPI_TWO_LINE_FULL && s->dma_directions==ABOX_SPI_DMA_RX)
        spi_disable(spi(id));
}
ABoxHalStatus ABoxSpiHw_DmaIdle(unsigned id)
{
    uint32_t p=spi(id); SpiInstance *s=&g_spi[id-1U];
    if (spi_i2s_flag_get(p,SPI_FLAG_CONFERR)!=RESET || spi_i2s_flag_get(p,SPI_FLAG_RXORERR)!=RESET) return ABOX_HAL_ERROR;
    return spi_i2s_flag_get(p,SPI_FLAG_TRANS)==RESET && (!(s->dma_directions & ABOX_SPI_DMA_TX) ||
           spi_i2s_flag_get(p,SPI_FLAG_TBE)!=RESET) ? ABOX_HAL_OK : ABOX_HAL_BUSY;
}
ABoxHalStatus ABoxI2sHw_Reserve(unsigned id)
{
    SpiInstance *s;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    s=&g_spi[id-1U];if (s->open) return ABOX_HAL_BUSY;
    s->open=2U;clock_enable(id);spi_i2s_deinit(spi(id));return ABOX_HAL_OK;
}
ABoxI2sRegisters *ABoxI2sHw_Registers(unsigned id)
{ return (ABoxI2sRegisters *)(uintptr_t)spi(id); }
void ABoxI2sHw_Release(unsigned id)
{ spi_i2s_deinit(spi(id));g_spi[id-1U].open=0U;clock_disable(id); }
ABoxI2sClockRegisters *ABoxI2sHw_ClockRegisters(void) { return (ABoxI2sClockRegisters *)(uintptr_t)RCU; }
uint32_t ABoxI2sHw_ExternalClockHz(void) { return HXTAL_VALUE; }
int ABoxI2sHw_FormatSupported(const ABoxI2sConfig *c) { (void)c;return 1; }
