#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_spi.h"
#include "abox_spi_dma_backend.h"
#include "abox_i2s_backend.h"
#include "abox_platform_port.h"

static unsigned clocks[4], initialized, transfers, fail_timeout, fail_error, tick_ready = 1U, configured_direction;
uint32_t fake_clock_regs[12];
#ifndef TEST_STM32
uint32_t fake_spi_data[3];
#endif
static uint16_t captured[8], pending;
static uint32_t now;
static uint32_t fake_tick(void *context) { (void)context; return now; }
static ABoxPlatformPort port = {.get_tick_ms = fake_tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return tick_ready ? &port : 0; }
uint32_t ABox_PortGetTickMs(void) { return now++; }
void fake_spi_clock(unsigned id, unsigned enabled) { clocks[id] = enabled; }

#ifdef TEST_STM32
#include "stm32f1xx_hal_spi.h"
SPI_TypeDef fake_spi_st[3] = {{1},{2},{3}};
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h)
{ assert(h->Instance);configured_direction=h->Init.Direction;initialized++;return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *h) { assert(h->Instance); return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, const uint8_t *tx,
                                         uint8_t *rx, uint16_t count, uint32_t timeout)
{
    unsigned width = h->Init.DataSize == SPI_DATASIZE_16BIT ? 2U : 1U;
    (void)timeout;
    if (fail_timeout) return HAL_TIMEOUT;
    if (fail_error) return HAL_ERROR;
    for (unsigned i = 0; i < count; ++i) {
        uint16_t v = 0; memcpy(&v, tx + i * width, width); captured[transfers++] = v;
        v ^= 0x5aU; memcpy(rx + i * width, &v, width);
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, const uint8_t *tx, uint16_t count, uint32_t timeout)
{
    unsigned width = h->Init.DataSize == SPI_DATASIZE_16BIT ? 2U : 1U;
    (void)timeout;
    if (fail_timeout) return HAL_TIMEOUT;
    if (fail_error) return HAL_ERROR;
    for (unsigned i = 0; i < count; ++i) { uint16_t v = 0; memcpy(&v, tx + i * width, width); captured[transfers++] = v; }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *h,uint8_t *rx,uint16_t count,uint32_t timeout)
{
    unsigned width=h->Init.DataSize==SPI_DATASIZE_16BIT?2U:1U;
    (void)timeout;if(fail_timeout)return HAL_TIMEOUT;if(fail_error)return HAL_ERROR;
    for(unsigned i=0U;i<count;++i) { uint16_t value=0x66U;memcpy(rx+i*width,&value,width); }
    return HAL_OK;
}
#else
#include "gd32f10x_spi.h"
void rcu_periph_clock_enable(uint32_t id) { fake_spi_clock(id,1); }
void rcu_periph_clock_disable(uint32_t id) { fake_spi_clock(id,0); }
void spi_struct_para_init(spi_parameter_struct *p) { memset(p,0,sizeof(*p)); }
void spi_i2s_deinit(uint32_t id) { assert(id >= 1U && id <= 3U); }
void spi_init(uint32_t id, spi_parameter_struct *p) { assert(id);configured_direction=p->trans_mode;initialized++; }
void spi_nss_output_enable(uint32_t id) { assert(id); }
void spi_enable(uint32_t id) { assert(id); }
void spi_disable(uint32_t id) { assert(id); }
void spi_bidirectional_transfer_config(uint32_t id,uint32_t direction) { assert(id && direction<=1U); }
void spi_dma_enable(uint32_t id,uint8_t mode) { assert(id && mode<=1U); }
void spi_dma_disable(uint32_t id,uint8_t mode) { assert(id && mode<=1U); }
FlagStatus spi_i2s_flag_get(uint32_t id, uint32_t flag)
{
    assert(id);
    if (flag == SPI_FLAG_CONFERR) return fail_error ? SET : RESET;
    if (flag == SPI_FLAG_RXORERR || flag == SPI_FLAG_TRANS) return RESET;
    return fail_timeout ? RESET : SET;
}
void spi_i2s_data_transmit(uint32_t id, uint16_t value)
{ assert(id); captured[transfers++] = value; pending = value ^ 0x5aU; }
uint16_t spi_i2s_data_receive(uint32_t id) { assert(id); return pending; }
#endif

int main(void)
{
    {
        ABoxI2sConfig audio={0};audio.mode=ABOX_I2S_MASTER_TX;audio.standard=ABOX_I2S_PCM_LONG;audio.format=ABOX_I2S_16_IN_32;
#ifdef TEST_STM32
        assert(!ABoxI2sHw_FormatSupported(&audio));audio.mode=ABOX_I2S_SLAVE_TX;assert(ABoxI2sHw_FormatSupported(&audio));
#else
        assert(ABoxI2sHw_FormatSupported(&audio));
#endif
    }
    ABoxSpiConfig c = {ABOX_SPI_MASTER, ABOX_SPI_NSS_SOFTWARE, 32U, 8U, 1U, 1U, 0U, ABOX_SPI_TWO_LINE_FULL};
    uint8_t tx[] = {0x12,0x34}, rx[2] = {0,0};
    uint16_t tx16[] = {0x1234,0xabcd}, rx16[2] = {0,0};
    assert(ABoxSpi_Open(0, &c) == ABOX_HAL_INVALID);
    c.prescaler = 3U; assert(ABoxSpi_Open(1, &c) == ABOX_HAL_INVALID); c.prescaler = 32U;
    c.role = ABOX_SPI_SLAVE; c.nss = ABOX_SPI_NSS_OUTPUT;
    assert(ABoxSpi_Open(1, &c) == ABOX_HAL_INVALID); c.role = ABOX_SPI_MASTER; c.nss = ABOX_SPI_NSS_SOFTWARE;
    assert(!initialized && !clocks[1]);
    assert(ABoxSpi_Open(1, &c) == ABOX_HAL_OK && clocks[1]);
    assert(ABoxSpi_Open(1, &c) == ABOX_HAL_BUSY);
    { uint8_t width=0U,directions=0U;
      assert(ABoxSpiHw_DmaReserve(1,tx,rx,2U,&width,&directions)==ABOX_HAL_OK && width==1U && directions==3U);
      assert(ABoxSpi_Close(1)==ABOX_HAL_BUSY && ABoxSpi_Transfer(1,tx,rx,2U,5U)==ABOX_HAL_BUSY);
      assert(ABoxSpiHw_DmaAddress(1)!=0U);ABoxSpiHw_DmaRequest(1,1);ABoxSpiHw_DmaRequest(1,0);
      ABoxSpiHw_DmaRelease(1,0);
    }
    assert(ABoxSpi_Transfer(1, 0, 0, 1, 5) == ABOX_HAL_INVALID);
    assert(ABoxSpi_Transfer(1, tx, tx + 1, 2, 5) == ABOX_HAL_INVALID);
    tick_ready = 0; assert(ABoxSpi_Transfer(1, tx, rx, 2, 5) == ABOX_HAL_INVALID); tick_ready = 1;
    assert(ABoxSpi_Transfer(1, tx, rx, 2, 5) == ABOX_HAL_OK);
    assert(rx[0] == (0x12 ^ 0x5a) && rx[1] == (0x34 ^ 0x5a));
    transfers = 0; memcpy(rx, tx, sizeof(tx));
    assert(ABoxSpi_Transfer(1, rx, rx, 2, 5) == ABOX_HAL_OK && rx[1] == (0x34 ^ 0x5a));
    transfers = 0; assert(ABoxSpi_Transfer(1, 0, rx, 2, 5) == ABOX_HAL_OK);
    assert(captured[0] == 0xffU && captured[1] == 0xffU && rx[0] == (0xff ^ 0x5a));
    transfers = 0; assert(ABoxSpi_Transfer(1, tx, 0, 2, 5) == ABOX_HAL_OK && captured[1] == 0x34);
    fail_timeout = 1; assert(ABoxSpi_Transfer(1, tx, rx, 2, 2) == ABOX_HAL_TIMEOUT);
    fail_timeout = 0; assert(ABoxSpi_Transfer(1, tx, rx, 2, 2) == ABOX_HAL_ERROR);
    assert(ABoxSpi_Close(1) == ABOX_HAL_OK && !clocks[1]);
    c.bits = 16U; assert(ABoxSpi_Open(3, &c) == ABOX_HAL_OK && clocks[3]);
    assert(ABoxSpi_Transfer(3, (const uint8_t *)tx16 + 1, rx16, 1, 5) == ABOX_HAL_INVALID);
    transfers = 0; assert(ABoxSpi_Transfer(3, tx16, rx16, 2, 5) == ABOX_HAL_OK);
    assert(rx16[0] == (0x1234U ^ 0x5aU) && rx16[1] == (0xabcdU ^ 0x5aU));
    fail_error = 1; assert(ABoxSpi_Transfer(3, tx16, rx16, 1, 5) == ABOX_HAL_ERROR); fail_error = 0;
    assert(ABoxSpi_Close(3) == ABOX_HAL_OK && !clocks[3]);
    assert(ABoxSpi_Close(3) == ABOX_HAL_INVALID);
    assert(ABoxI2sHw_Reserve(2U)==ABOX_HAL_OK);
    assert(ABoxSpi_Open(2U,&c)==ABOX_HAL_BUSY && ABoxSpi_Close(2U)==ABOX_HAL_INVALID);
    ABoxI2sHw_Release(2U);
    c.bits=8U;c.wiring=ABOX_SPI_TWO_LINE_RX;
    assert(ABoxSpi_Open(1U,&c)==ABOX_HAL_OK && configured_direction==1U);
    assert(ABoxSpi_Transfer(1U,tx,rx,1U,5U)==ABOX_HAL_INVALID);
    { uint8_t width=0U,directions=0U;
      assert(ABoxSpiHw_DmaReserve(1U,0,rx,1U,&width,&directions)==ABOX_HAL_OK && directions==ABOX_SPI_DMA_RX);
      ABoxSpiHw_DmaRequest(1U,1);ABoxSpiHw_DmaHaltReceive(1U);ABoxSpiHw_DmaRequest(1U,0);ABoxSpiHw_DmaRelease(1U,0);
    }
    pending=0x66U;assert(ABoxSpi_Transfer(1U,0,rx,1U,5U)==ABOX_HAL_OK && rx[0]==0x66U);
    assert(ABoxSpi_Close(1U)==ABOX_HAL_OK);
    c.wiring=ABOX_SPI_ONE_LINE;assert(ABoxSpi_Open(1U,&c)==ABOX_HAL_OK && configured_direction==2U);
    assert(ABoxSpi_Transfer(1U,tx,rx,1U,5U)==ABOX_HAL_INVALID);
    { uint8_t width=0U,directions=0U;
      assert(ABoxSpiHw_DmaReserve(1U,tx,0,1U,&width,&directions)==ABOX_HAL_OK && directions==ABOX_SPI_DMA_TX);
      ABoxSpiHw_DmaRequest(1U,1);ABoxSpiHw_DmaRequest(1U,0);ABoxSpiHw_DmaRelease(1U,0);
      assert(ABoxSpiHw_DmaReserve(1U,0,rx,1U,&width,&directions)==ABOX_HAL_OK && directions==ABOX_SPI_DMA_RX);
      ABoxSpiHw_DmaRequest(1U,1);ABoxSpiHw_DmaHaltReceive(1U);ABoxSpiHw_DmaRequest(1U,0);ABoxSpiHw_DmaRelease(1U,0);
    }
    transfers=0U;assert(ABoxSpi_Transfer(1U,tx,0,2U,5U)==ABOX_HAL_OK && captured[1]==0x34U);
    pending=0x66U;assert(ABoxSpi_Transfer(1U,0,rx,1U,5U)==ABOX_HAL_OK && rx[0]==0x66U);
    assert(ABoxSpi_Close(1U)==ABOX_HAL_OK);
    return 0;
}
