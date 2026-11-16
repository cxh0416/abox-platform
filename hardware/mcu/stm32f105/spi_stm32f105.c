#include <string.h>
#include <stddef.h>
#include "abox_spi_common.h"
#include "abox_spi_dma_backend.h"
#include "abox_i2s_backend.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_spi.h"
#if defined(STM32F105xC)
_Static_assert(offsetof(ABoxI2sRegisters,data)==offsetof(SPI_TypeDef,DR) &&
               offsetof(ABoxI2sRegisters,i2s_config)==offsetof(SPI_TypeDef,I2SCFGR) &&
               offsetof(ABoxI2sRegisters,i2s_prescaler)==offsetof(SPI_TypeDef,I2SPR),"I2S register layout");
_Static_assert(SPI_I2SCFGR_I2SMOD==(1U<<11) && SPI_I2SCFGR_I2SE==(1U<<10) &&
               SPI_I2SCFGR_I2SCFG==(3U<<8) && SPI_I2SCFGR_I2SSTD==(3U<<4) &&
               SPI_I2SCFGR_DATLEN==(3U<<1) && SPI_CR2_RXDMAEN==1U && SPI_CR2_TXDMAEN==2U,"I2S control bits");
_Static_assert(offsetof(ABoxI2sRegisters,crc_poly)==offsetof(SPI_TypeDef,CRCPR) &&
               SPI_CR1_SPE==(1U<<6) && SPI_CR1_CRCNEXT==(1U<<12) && SPI_CR1_CRCEN==(1U<<13) &&
               SPI_SR_RXNE==1U && SPI_SR_TXE==2U && SPI_SR_CRCERR==(1U<<4),"SPI CRC layout");
_Static_assert(offsetof(ABoxI2sClockRegisters,config)==offsetof(RCC_TypeDef,CFGR2) &&
               RCC_CR_HSERDY==(1U<<17) && RCC_CR_PLL2ON==(1U<<26) && RCC_CR_PLL3ON==(1U<<28) &&
               RCC_CR_PLL3RDY==(1U<<29) && RCC_CFGR2_PREDIV2==(15U<<4) && RCC_CFGR2_PLL3MUL==(15U<<12) &&
               RCC_CFGR2_I2S2SRC==(1U<<17) && RCC_CFGR2_I2S3SRC==(1U<<18),"I2S clock layout");
#endif

typedef struct {
    SPI_HandleTypeDef handle;
    ABoxSpiConfig config;
    uint8_t open, fault, dma_busy, dma_directions;
} SpiInstance;
static SpiInstance g_spi[3];

static SPI_TypeDef *spi(unsigned id)
{
    switch (id) { case 1: return SPI1; case 2: return SPI2; case 3: return SPI3; default: return 0; }
}
static void clock_enable(unsigned id)
{
    switch (id) { case 1: __HAL_RCC_SPI1_CLK_ENABLE(); break; case 2: __HAL_RCC_SPI2_CLK_ENABLE(); break;
                  case 3: __HAL_RCC_SPI3_CLK_ENABLE(); break; default: break; }
}
static void clock_disable(unsigned id)
{
    switch (id) { case 1: __HAL_RCC_SPI1_CLK_DISABLE(); break; case 2: __HAL_RCC_SPI2_CLK_DISABLE(); break;
                  case 3: __HAL_RCC_SPI3_CLK_DISABLE(); break; default: break; }
}
static ABoxHalStatus status(HAL_StatusTypeDef s)
{
    if (s == HAL_OK) return ABOX_HAL_OK;
    if (s == HAL_BUSY) return ABOX_HAL_BUSY;
    if (s == HAL_TIMEOUT) return ABOX_HAL_TIMEOUT;
    return ABOX_HAL_ERROR;
}

ABoxHalStatus ABoxSpi_Open(unsigned id, const ABoxSpiConfig *config)
{
    SpiInstance *s;
    unsigned exponent = 0U;
    uint16_t divisor;
    HAL_StatusTypeDef result;
    if (!ABoxSpi_ConfigValid(id, config)) return ABOX_HAL_INVALID;
    s = &g_spi[id - 1U];
    if (s->open) return ABOX_HAL_BUSY;
    memset(&s->handle, 0, sizeof(s->handle));
    s->handle.Instance = spi(id);
    s->handle.Init.Mode = config->role == ABOX_SPI_MASTER ? SPI_MODE_MASTER : SPI_MODE_SLAVE;
    s->handle.Init.Direction = config->wiring == ABOX_SPI_TWO_LINE_RX ? SPI_DIRECTION_2LINES_RXONLY :
                               config->wiring == ABOX_SPI_ONE_LINE ? SPI_DIRECTION_1LINE : SPI_DIRECTION_2LINES;
    s->handle.Init.DataSize = config->bits == 16U ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
    s->handle.Init.CLKPolarity = config->cpol ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
    s->handle.Init.CLKPhase = config->cpha ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    s->handle.Init.NSS = config->nss == ABOX_SPI_NSS_SOFTWARE ? SPI_NSS_SOFT :
                         config->nss == ABOX_SPI_NSS_OUTPUT ? SPI_NSS_HARD_OUTPUT : SPI_NSS_HARD_INPUT;
    for (divisor = config->prescaler; divisor > 2U; divisor >>= 1U) ++exponent;
    s->handle.Init.BaudRatePrescaler = exponent << SPI_CR1_BR_Pos;
    s->handle.Init.FirstBit = config->lsb_first ? SPI_FIRSTBIT_LSB : SPI_FIRSTBIT_MSB;
    s->handle.Init.TIMode = SPI_TIMODE_DISABLE;
    s->handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s->handle.Init.CRCPolynomial = 7U;
    clock_enable(id);
    result = HAL_SPI_Init(&s->handle);
    if (result != HAL_OK) {
        (void)HAL_SPI_DeInit(&s->handle);
        clock_disable(id);
        return status(result);
    }
    s->config = *config; s->fault = 0U; s->open = 1U;
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxSpi_Close(unsigned id)
{
    SpiInstance *s;
    HAL_StatusTypeDef result;
    if (!spi(id)) return ABOX_HAL_INVALID;
    s = &g_spi[id - 1U];
    if (s->open!=1U) return ABOX_HAL_INVALID;
    if (s->dma_busy) return ABOX_HAL_BUSY;
    result = HAL_SPI_DeInit(&s->handle);
    if (result != HAL_OK) return status(result);
    clock_disable(id);
    s->open = 0U; s->fault = 0U;
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxSpi_Transfer(unsigned id, const void *tx, void *rx,
                               uint16_t frames, uint32_t timeout_ms)
{
    SpiInstance *s;
    HAL_StatusTypeDef result;
    if (!spi(id)) return ABOX_HAL_INVALID;
    s = &g_spi[id - 1U];
    if (s->open!=1U || !ABoxSpi_TransferValid(&s->config, tx, rx, frames) || !ABoxSpi_TickReady())
        return ABOX_HAL_INVALID;
    if (s->dma_busy) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    if (s->config.wiring != ABOX_SPI_TWO_LINE_FULL) {
        result = rx ? HAL_SPI_Receive(&s->handle, (uint8_t *)rx, frames, timeout_ms) :
                      HAL_SPI_Transmit(&s->handle, (const uint8_t *)tx, frames, timeout_ms);
        if (result != HAL_OK && result != HAL_BUSY) s->fault = 1U;
        return status(result);
    }
    if (!tx) {
        /* In two-line master mode HAL_SPI_Receive transmits the contents of
         * the RX buffer. Initialize those bytes to define the dummy value. */
        memset(rx, 0xff, (size_t)frames * (s->config.bits / 8U));
        tx = rx;
    }
    result = rx ? HAL_SPI_TransmitReceive(&s->handle, (const uint8_t *)tx, (uint8_t *)rx, frames, timeout_ms) :
                  HAL_SPI_Transmit(&s->handle, (const uint8_t *)tx, frames, timeout_ms);
    if (result != HAL_OK && result != HAL_BUSY) s->fault = 1U;
    return status(result);
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
uintptr_t ABoxSpiHw_DmaAddress(unsigned id) { return (uintptr_t)&spi(id)->DR; }
void ABoxSpiHw_DmaRequest(unsigned id,int enable)
{
    SPI_TypeDef *p=spi(id); SpiInstance *s=&g_spi[id-1U];
    if (enable) {
        if (s->config.wiring==ABOX_SPI_ONE_LINE) {
            __HAL_SPI_DISABLE(&s->handle);
            if (s->dma_directions & ABOX_SPI_DMA_TX) SPI_1LINE_TX(&s->handle);
            else SPI_1LINE_RX(&s->handle);
        }
        if (s->dma_directions & ABOX_SPI_DMA_RX) p->CR2 |= SPI_CR2_RXDMAEN;
        if (s->dma_directions & ABOX_SPI_DMA_TX) p->CR2 |= SPI_CR2_TXDMAEN;
        __HAL_SPI_ENABLE(&s->handle);
    } else p->CR2 &= ~(SPI_CR2_RXDMAEN|SPI_CR2_TXDMAEN);
}
void ABoxSpiHw_DmaHaltReceive(unsigned id)
{
    SpiInstance *s=&g_spi[id-1U];
    if (s->config.wiring!=ABOX_SPI_TWO_LINE_FULL && s->dma_directions==ABOX_SPI_DMA_RX)
        __HAL_SPI_DISABLE(&s->handle);
}
ABoxHalStatus ABoxSpiHw_DmaIdle(unsigned id)
{
    uint32_t status=spi(id)->SR; SpiInstance *s=&g_spi[id-1U];
    if (status & (SPI_SR_MODF|SPI_SR_OVR)) return ABOX_HAL_ERROR;
    return !(status & SPI_SR_BSY) && (!(s->dma_directions & ABOX_SPI_DMA_TX) || (status & SPI_SR_TXE)) ?
           ABOX_HAL_OK : ABOX_HAL_BUSY;
}
ABoxHalStatus ABoxI2sHw_Reserve(unsigned id)
{
    SpiInstance *s;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    s=&g_spi[id-1U];if (s->open) return ABOX_HAL_BUSY;
    s->open=2U;clock_enable(id);return ABOX_HAL_OK;
}
ABoxI2sRegisters *ABoxI2sHw_Registers(unsigned id)
{ return (ABoxI2sRegisters *)(void *)spi(id); }
void ABoxI2sHw_Release(unsigned id)
{ g_spi[id-1U].open=0U;clock_disable(id); }
ABoxI2sClockRegisters *ABoxI2sHw_ClockRegisters(void) { return (ABoxI2sClockRegisters *)(void *)RCC; }
uint32_t ABoxI2sHw_ExternalClockHz(void) { return HSE_VALUE; }
int ABoxI2sHw_FormatSupported(const ABoxI2sConfig *c)
{
    /* STM32F105 erratum 2.11.1: WS period is wrong for this master format. */
    return !(c->mode>=ABOX_I2S_MASTER_TX && c->standard==ABOX_I2S_PCM_LONG && c->format==ABOX_I2S_16_IN_32);
}
