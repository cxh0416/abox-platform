#include <assert.h>
#include <stdint.h>
#include "abox_spi.h"
#include "abox_spi_dma_backend.h"
#include "abox_i2s_backend.h"
#include "abox_platform_port.h"

static ABoxI2sRegisters hw;
static unsigned busy,fault,width=1U,directions=ABOX_SPI_DMA_RX|ABOX_SPI_DMA_TX,tick,inject_crc_error;
static unsigned claimed[8],blocked_channel;
int ABoxDmaResource_Claim(unsigned controller,unsigned channel)
{ assert(controller==1U && channel>=2U && channel<=3U);if(channel==blocked_channel || claimed[channel])return 0;claimed[channel]=1U;return 1; }
void ABoxDmaResource_Release(unsigned controller,unsigned channel)
{ assert(controller==1U && claimed[channel]);claimed[channel]=0U; }
static uint32_t get_tick(void *context) { (void)context;return tick; }
static ABoxPlatformPort platform_port={.get_tick_ms=get_tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return &platform_port; }
uint32_t ABox_PortGetTickMs(void) { return tick++; }
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t key)
{ assert(!key);if(inject_crc_error && (hw.control1&(1U<<12)))hw.status|=1U<<4; }
ABoxHalStatus ABoxSpiHw_DmaReserve(unsigned id,const void *tx,void *rx,uint16_t n,uint8_t *w,uint8_t *d)
{
    if (id!=1U || (!tx && !rx) || !n) return ABOX_HAL_INVALID;
    if (busy) return ABOX_HAL_BUSY;
    if (fault) return ABOX_HAL_ERROR;
    busy=1U;*w=(uint8_t)width;*d=(uint8_t)directions;return ABOX_HAL_OK;
}
void ABoxSpiHw_DmaRelease(unsigned id,int error) { assert(id==1U && busy);busy=0U;fault=(unsigned)error; }
ABoxI2sRegisters *ABoxI2sHw_Registers(unsigned id) { assert(id==1U);return &hw; }
int main(void)
{
    uint8_t tx[2]={0x12U,0x34U},rx[2]={0U,0U};
    assert(ABoxSpi_TransferCrc(1U,tx,rx,2U,2U,5U)==ABOX_HAL_INVALID);
    hw.status=3U;
    assert(ABoxSpi_TransferCrc(1U,tx,rx,2U,0x107U,5U)==ABOX_HAL_UNSUPPORTED && !busy && !fault);
    directions=ABOX_SPI_DMA_RX|ABOX_SPI_DMA_TX;
    blocked_channel=3U;
    assert(ABoxSpi_TransferCrc(1U,tx,rx,2U,7U,5U)==ABOX_HAL_BUSY && !busy && !claimed[2]);
    blocked_channel=0U;
    assert(ABoxSpi_TransferCrc(1U,tx,rx,2U,7U,5U)==ABOX_HAL_OK);
    assert(rx[0]==tx[0] && rx[1]==tx[1] && hw.crc_poly==7U && !(hw.control1&((1U<<6)|(1U<<12)|(1U<<13))));
    inject_crc_error=1U;
    assert(ABoxSpi_TransferCrc(1U,tx,rx,1U,7U,5U)==ABOX_HAL_ERROR && fault);
    fault=0U;inject_crc_error=0U;hw.status=0U;
    assert(ABoxSpi_TransferCrc(1U,tx,rx,1U,7U,3U)==ABOX_HAL_TIMEOUT && fault && !busy);
    fault=0U;hw.status=3U;width=2U;
    { uint16_t a=0x1234U,b=0U;
      assert(ABoxSpi_TransferCrc(1U,&a,&b,1U,0x1021U,5U)==ABOX_HAL_OK && b==a); }
    width=1U;directions=ABOX_SPI_DMA_TX;hw.control1=1U<<15;hw.status=2U;
    assert(ABoxSpi_TransferCrc(1U,tx,0,2U,7U,5U)==ABOX_HAL_OK && (hw.control1&(1U<<14)));
    directions=ABOX_SPI_DMA_RX;hw.control1=(1U<<15)|(1U<<14);hw.status=3U;hw.data=0x42U;
    assert(ABoxSpi_TransferCrc(1U,0,rx,2U,7U,5U)==ABOX_HAL_OK && rx[0]==0x42U && rx[1]==0x42U);
    assert(!(hw.control1&(1U<<14)));
    hw.status=3U;hw.data=0x31U;
    assert(ABoxSpi_TransferCrc(1U,0,rx,1U,7U,5U)==ABOX_HAL_OK && rx[0]==0x31U);
    hw.control1=0U;hw.status=3U;hw.data=0x52U;
    assert(ABoxSpi_TransferCrc(1U,0,rx,1U,7U,5U)==ABOX_HAL_OK && rx[0]==0x52U);
    return 0;
}
