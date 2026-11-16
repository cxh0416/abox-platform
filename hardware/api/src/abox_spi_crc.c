#include "abox_spi.h"
#include "abox_spi_common.h"
#include "abox_spi_dma_backend.h"
#include "abox_i2s_backend.h"
#include "abox_critical.h"
#include "abox_dma_resource.h"
#include <string.h>

enum { SPI_ENABLE=1U<<6, SPI_CRC_NEXT=1U<<12, SPI_CRC_ENABLE=1U<<13,
       SPI_ONE_LINE_OUTPUT=1U<<14, SPI_ONE_LINE=1U<<15,
       SPI_RX_READY=1U, SPI_TX_READY=1U<<1, SPI_CRC_ERROR=1U<<4,
       SPI_MODE_ERROR=1U<<5, SPI_OVERRUN=1U<<6, SPI_BUSY=1U<<7 };
static ABoxHalStatus wait_status(ABoxI2sRegisters *r,uint32_t mask,int set,uint32_t start,uint32_t timeout)
{
    for (;;) {
        uint32_t status=r->status;
        if (status & (SPI_MODE_ERROR|SPI_OVERRUN)) return ABOX_HAL_ERROR;
        if (!!(status & mask)==set) return ABOX_HAL_OK;
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout) return ABOX_HAL_TIMEOUT;
    }
}
ABoxHalStatus ABoxSpi_TransferCrc(unsigned id,const void *tx,void *rx,uint16_t frames,
                                  uint16_t polynomial,uint32_t timeout)
{
    ABoxI2sRegisters *r;ABoxHalStatus status;uint8_t width,directions;uint32_t start,key;
    const uint8_t *source=(const uint8_t *)tx;uint8_t *dest=(uint8_t *)rx;
    volatile uint32_t discard;unsigned controller,rx_channel,tx_channel;
    if (!polynomial || !(polynomial&1U) || !ABoxSpi_TickReady()) return ABOX_HAL_INVALID;
    status=ABoxSpiHw_DmaReserve(id,tx,rx,frames,&width,&directions);
    if (status!=ABOX_HAL_OK) return status;
    if (width==1U && polynomial>255U) {
        ABoxSpiHw_DmaRelease(id,0);return ABOX_HAL_UNSUPPORTED;
    }
    /* STM32F105 erratum 2.10.2: unrelated transfers on the SPI's mapped DMA
     * channels can corrupt a CPU-driven CRC. Reserve both even though this
     * operation does not start DMA; board code excludes external DMA users. */
    controller=id==3U ? 2U : 1U;
    rx_channel=id==1U ? 2U : id==2U ? 4U : 1U;
    tx_channel=id==1U ? 3U : id==2U ? 5U : 2U;
    if (!ABoxDmaResource_Claim(controller,rx_channel)) { ABoxSpiHw_DmaRelease(id,0);return ABOX_HAL_BUSY; }
    if (!ABoxDmaResource_Claim(controller,tx_channel)) {
        ABoxDmaResource_Release(controller,rx_channel);ABoxSpiHw_DmaRelease(id,0);return ABOX_HAL_BUSY;
    }
    r=ABoxI2sHw_Registers(id);
    if (!r) { status=ABOX_HAL_ERROR;goto release; }
    start=ABox_PortGetTickMs();status=wait_status(r,SPI_BUSY,0,start,timeout);
    if (status!=ABOX_HAL_OK) goto done;
    r->control1 &= ~(SPI_ENABLE|SPI_CRC_ENABLE|SPI_CRC_NEXT);
    if (r->control1 & SPI_ONE_LINE) {
        if (directions==ABOX_SPI_DMA_TX) r->control1 |= SPI_ONE_LINE_OUTPUT;
        else r->control1 &= ~SPI_ONE_LINE_OUTPUT;
    }
    discard=r->data;discard=r->status;(void)discard;
    r->crc_poly=polynomial;r->status &= ~SPI_CRC_ERROR;
    r->control1 |= SPI_CRC_ENABLE;
    key=ABoxCritical_Enter();r->control1 |= SPI_ENABLE;
    if (directions==ABOX_SPI_DMA_RX && frames==1U) r->control1 |= SPI_CRC_NEXT;
    ABoxCritical_Exit(key);
    for (unsigned i=0U;i<frames;++i) {
        uint16_t value=width==1U ? 0xffU : 0xffffU;
        if (directions & ABOX_SPI_DMA_TX) {
            status=wait_status(r,SPI_TX_READY,1,start,timeout);if (status!=ABOX_HAL_OK) goto done;
            if (source) memcpy(&value,source+i*width,width);
            key=i+1U==frames ? ABoxCritical_Enter() : 0U;
            if (width==1U) *(volatile uint8_t *)&r->data=(uint8_t)value;else r->data=value;
            if (i+1U==frames) { r->control1 |= SPI_CRC_NEXT;ABoxCritical_Exit(key); }
        }
        if (directions & ABOX_SPI_DMA_RX) {
            status=wait_status(r,SPI_RX_READY,1,start,timeout);if (status!=ABOX_HAL_OK) goto done;
            if (directions==ABOX_SPI_DMA_RX && i+2U==frames) {
                key=ABoxCritical_Enter();value=(uint16_t)r->data;r->control1 |= SPI_CRC_NEXT;ABoxCritical_Exit(key);
            } else value=(uint16_t)r->data;
            if (dest) memcpy(dest+i*width,&value,width);
        }
    }
    if (directions & ABOX_SPI_DMA_RX) {
        status=wait_status(r,SPI_RX_READY,1,start,timeout);if (status!=ABOX_HAL_OK) goto done;
        if (directions==ABOX_SPI_DMA_RX) r->control1 &= ~SPI_ENABLE;
        discard=r->data;(void)discard;
    }
    if (directions & ABOX_SPI_DMA_TX) {
        status=wait_status(r,SPI_TX_READY,1,start,timeout);if (status!=ABOX_HAL_OK) goto done;
    }
    status=wait_status(r,SPI_BUSY,0,start,timeout);if (status!=ABOX_HAL_OK) goto done;
    if ((directions & ABOX_SPI_DMA_RX) && (r->status & SPI_CRC_ERROR)) status=ABOX_HAL_ERROR;
done:
    r->control1 &= ~(SPI_ENABLE|SPI_CRC_ENABLE|SPI_CRC_NEXT);
release:
    ABoxDmaResource_Release(controller,tx_channel);ABoxDmaResource_Release(controller,rx_channel);
    ABoxSpiHw_DmaRelease(id,status!=ABOX_HAL_OK);return status;
}
