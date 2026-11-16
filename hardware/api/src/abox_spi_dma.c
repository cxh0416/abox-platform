#include "abox_spi_dma_backend.h"
#include "abox_spi_common.h"
#include "abox_dma.h"
#include "abox_critical.h"
#include "abox_i2s_backend.h"

enum { CRC_SPI_ENABLE=1U<<6, CRC_NEXT=1U<<12, CRC_ENABLE=1U<<13,
       CRC_RX_READY=1U, CRC_ERROR=1U<<4, CRC_MODE_ERROR=1U<<5, CRC_OVERRUN=1U<<6, CRC_BUSY=1U<<7 };

typedef struct {
    const ABoxSpiDmaEvents *events;
    ABoxDmaEvents rx_events,tx_events;
    uint16_t dummy_tx,dummy_rx;
    uint8_t owned,rx_open,tx_open,expected,crc;
    volatile uint8_t done,error,notified;
} SpiDma;
static SpiDma transfers[3];
static unsigned controller(unsigned id) { return id==3U ? 2U : 1U; }
static unsigned rx_channel(unsigned id) { static const uint8_t c[]={0U,2U,4U,1U};return c[id]; }
static unsigned tx_channel(unsigned id) { static const uint8_t c[]={0U,3U,5U,2U};return c[id]; }
static void on_dma(void *context,unsigned ctl,unsigned channel,ABoxDmaEvent event)
{
    SpiDma *s=(SpiDma *)context; unsigned id=(unsigned)(s-transfers)+1U;
    const ABoxSpiDmaEvents *events=s->events;
    uint32_t key=ABoxCritical_Enter();
    (void)ctl;
    if (!s->owned || event==ABOX_DMA_HALF) { ABoxCritical_Exit(key); return; }
    if (event==ABOX_DMA_TRANSFER_ERROR) {
        s->error=1U; ABoxSpiHw_DmaRequest(id,0); ABoxSpiHw_DmaHaltReceive(id);
    } else if (channel==rx_channel(id)) {
        s->done|=ABOX_SPI_DMA_RX;
        /* CRC still has to arrive after the receive DMA payload. */
        if (!s->crc || s->expected!=ABOX_SPI_DMA_RX) ABoxSpiHw_DmaHaltReceive(id);
    } else if (channel==tx_channel(id)) s->done|=ABOX_SPI_DMA_TX;
    if (!s->notified && (s->error || s->done==s->expected)) {
        s->notified=1U;
        if (events && events->buffers_ready)
            events->buffers_ready(events->context,id,s->error ? ABOX_HAL_ERROR : ABOX_HAL_OK);
    }
    ABoxCritical_Exit(key);
}
static ABoxHalStatus cleanup(unsigned id,int fault)
{
    SpiDma *s=&transfers[id-1U]; ABoxHalStatus status;
    ABoxSpiHw_DmaRequest(id,0);
    ABoxSpiHw_DmaHaltReceive(id);
    if (s->rx_open) {
        status=ABoxDma_Abort(controller(id),rx_channel(id)); if (status!=ABOX_HAL_OK) return status;
        status=ABoxDma_Close(controller(id),rx_channel(id)); if (status!=ABOX_HAL_OK) return status;
        s->rx_open=0U;
    }
    if (s->tx_open) {
        status=ABoxDma_Abort(controller(id),tx_channel(id)); if (status!=ABOX_HAL_OK) return status;
        status=ABoxDma_Close(controller(id),tx_channel(id)); if (status!=ABOX_HAL_OK) return status;
        s->tx_open=0U;
    }
    if (s->crc) {
        ABoxI2sRegisters *r=ABoxI2sHw_Registers(id);
        r->control1 &= ~(CRC_SPI_ENABLE|CRC_ENABLE|CRC_NEXT);s->crc=0U;
    }
    s->owned=0U;s->events=0;ABoxSpiHw_DmaRelease(id,fault);return ABOX_HAL_OK;
}
static ABoxHalStatus start_dma(unsigned id,const void *tx,void *rx,uint16_t frames,uint16_t polynomial,
                               const ABoxSpiDmaEvents *events)
{
    SpiDma *s; ABoxHalStatus status; uint8_t width,directions;
    ABoxDmaConfig config={ABOX_DMA_PERIPHERAL_TO_MEMORY,1U,1U,0U,1U,0U,1U};
    if (id<1U || id>3U || (polynomial && !(polynomial&1U))) return ABOX_HAL_INVALID;
    s=&transfers[id-1U];
    if (s->owned) return ABOX_HAL_BUSY;
    status=ABoxSpiHw_DmaReserve(id,tx,rx,frames,&width,&directions); if (status!=ABOX_HAL_OK) return status;
    s->owned=1U;s->done=0U;s->error=0U;s->notified=0U;s->events=events;s->expected=directions;s->crc=0U;
    if (polynomial) {
        ABoxI2sRegisters *r=ABoxI2sHw_Registers(id);volatile uint32_t discard;
        if (width==1U && polynomial>255U) {
            status=ABOX_HAL_UNSUPPORTED;goto failed;
        }
        if (!r) { status=ABOX_HAL_ERROR;goto failed; }
        if (r->status & (CRC_BUSY|CRC_MODE_ERROR|CRC_OVERRUN)) { status=ABOX_HAL_BUSY;goto failed; }
        r->control1 &= ~(CRC_SPI_ENABLE|CRC_ENABLE|CRC_NEXT);
        discard=r->data;discard=r->status;(void)discard;
        r->status &= ~CRC_ERROR;r->crc_poly=polynomial;r->control1 |= CRC_ENABLE;s->crc=1U;
    }
    s->rx_events.context=s;s->rx_events.on_event=on_dma;
    s->tx_events.context=s;s->tx_events.on_event=on_dma;
    s->dummy_tx=0xFFFFU;s->dummy_rx=0U;
    config.peripheral_width=width;config.memory_width=width;config.memory_increment=rx ? 1U : 0U;
    if (directions & ABOX_SPI_DMA_RX) {
        status=ABoxDma_Open(controller(id),rx_channel(id),&config);if(status!=ABOX_HAL_OK) goto failed;
        s->rx_open=1U;
    }
    if (directions & ABOX_SPI_DMA_TX) {
        config.direction=ABOX_DMA_MEMORY_TO_PERIPHERAL;config.memory_increment=tx ? 1U : 0U;
        status=ABoxDma_Open(controller(id),tx_channel(id),&config);if(status!=ABOX_HAL_OK) goto failed;
        s->tx_open=1U;
    }
    if (directions & ABOX_SPI_DMA_RX) {
        status=ABoxDma_Start(controller(id),rx_channel(id),ABoxSpiHw_DmaAddress(id),
                             rx ? (uintptr_t)rx : (uintptr_t)&s->dummy_rx,frames,&s->rx_events);
        if(status!=ABOX_HAL_OK) goto failed;
    }
    if (directions & ABOX_SPI_DMA_TX) {
        status=ABoxDma_Start(controller(id),tx_channel(id),tx ? (uintptr_t)tx : (uintptr_t)&s->dummy_tx,
                             ABoxSpiHw_DmaAddress(id),frames,&s->tx_events);
        if(status!=ABOX_HAL_OK) goto failed;
    }
    ABoxSpiHw_DmaRequest(id,1);return ABOX_HAL_OK;
failed:
    (void)cleanup(id,0);return status;
}
ABoxHalStatus ABoxSpi_StartDma(unsigned id,const void *tx,void *rx,uint16_t frames,const ABoxSpiDmaEvents *events)
{ return start_dma(id,tx,rx,frames,0U,events); }
ABoxHalStatus ABoxSpi_StartDmaCrc(unsigned id,const void *tx,void *rx,uint16_t frames,
                                  uint16_t polynomial,const ABoxSpiDmaEvents *events)
{ return polynomial ? start_dma(id,tx,rx,frames,polynomial,events) : ABOX_HAL_INVALID; }
ABoxHalStatus ABoxSpi_FinishDma(unsigned id,uint32_t timeout_ms)
{
    SpiDma *s; ABoxHalStatus status; uint32_t start;
    if (id<1U || id>3U || !transfers[id-1U].owned) return ABOX_HAL_INVALID;
    s=&transfers[id-1U];
    if (s->error) { (void)cleanup(id,1); return ABOX_HAL_ERROR; }
    if (s->done!=s->expected) return ABOX_HAL_BUSY;
    if (!ABoxSpi_TickReady()) return ABOX_HAL_INVALID;
    start=ABox_PortGetTickMs();
    if (s->crc && (s->expected & ABOX_SPI_DMA_RX)) {
        ABoxI2sRegisters *r=ABoxI2sHw_Registers(id);volatile uint32_t discard;
        while (!(r->status & CRC_RX_READY)) {
            if (r->status & (CRC_MODE_ERROR|CRC_OVERRUN)) { (void)cleanup(id,1);return ABOX_HAL_ERROR; }
            if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout_ms) { (void)cleanup(id,1);return ABOX_HAL_TIMEOUT; }
        }
        if (s->expected==ABOX_SPI_DMA_RX) r->control1 &= ~CRC_SPI_ENABLE;
        discard=r->data;(void)discard;
        if (r->status & CRC_ERROR) { (void)cleanup(id,1);return ABOX_HAL_ERROR; }
    }
    do {
        status=ABoxSpiHw_DmaIdle(id);
        if (status==ABOX_HAL_OK) return cleanup(id,0);
        if (status!=ABOX_HAL_BUSY) { (void)cleanup(id,1); return status; }
    } while ((uint32_t)(ABox_PortGetTickMs()-start)<timeout_ms);
    (void)cleanup(id,1);return ABOX_HAL_TIMEOUT;
}
ABoxHalStatus ABoxSpi_AbortDma(unsigned id)
{
    if (id<1U || id>3U || !transfers[id-1U].owned) return ABOX_HAL_INVALID;
    return cleanup(id,1);
}
