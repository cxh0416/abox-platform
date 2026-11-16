#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_spi_dma_backend.h"
#include "abox_dma.h"
#include "abox_platform_port.h"
#include "abox_i2s_backend.h"
static ABoxI2sRegisters hw[3];
ABoxI2sRegisters *ABoxI2sHw_Registers(unsigned id) { assert(id>=1U && id<=3U);return &hw[id-1U]; }
static ABoxDmaConfig configs[2][7];
static const ABoxDmaEvents *dma_events[2][7];
static unsigned opened[2][7], stops, releases, requests, busy, fault, notifications, fail_ch, halts;
static uint8_t dma_directions=ABOX_SPI_DMA_RX|ABOX_SPI_DMA_TX;
static uintptr_t source[2][7],destination[2][7];
static uint32_t now;
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t key) { (void)key; }
static uint32_t tick(void *context) { (void)context;return now++; }
static ABoxPlatformPort port={.get_tick_ms=tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return &port; }
uint32_t ABox_PortGetTickMs(void) { return now++; }
ABoxHalStatus ABoxSpiHw_DmaReserve(unsigned id,const void *tx,void *rx,uint16_t n,uint8_t *width,uint8_t *directions)
{ if (id<1U||id>3U||(!tx&&!rx)||!n) return ABOX_HAL_INVALID;if(busy)return ABOX_HAL_BUSY;busy=1U;*width=1U;*directions=dma_directions;return ABOX_HAL_OK; }
uintptr_t ABoxSpiHw_DmaAddress(unsigned id) { return 0x40000000U+id*4U; }
void ABoxSpiHw_DmaRequest(unsigned id,int enable) { assert(id>=1U&&id<=3U);requests=(unsigned)enable; }
void ABoxSpiHw_DmaHaltReceive(unsigned id) { assert(id>=1U&&id<=3U);if(dma_directions==ABOX_SPI_DMA_RX)++halts; }
ABoxHalStatus ABoxSpiHw_DmaIdle(unsigned id) { (void)id;return fault ? ABOX_HAL_BUSY : ABOX_HAL_OK; }
void ABoxSpiHw_DmaRelease(unsigned id,int f) { (void)id;busy=0U;fault=(unsigned)f; }
ABoxHalStatus ABoxDma_Open(unsigned ctl,unsigned ch,const ABoxDmaConfig *cfg)
{ if(ch==fail_ch)return ABOX_HAL_BUSY;assert(!opened[ctl-1U][ch-1U]);opened[ctl-1U][ch-1U]=1U;configs[ctl-1U][ch-1U]=*cfg;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Start(unsigned ctl,unsigned ch,uintptr_t src,uintptr_t dst,uint16_t n,const ABoxDmaEvents *events)
{ assert(opened[ctl-1U][ch-1U]&&n==3U);source[ctl-1U][ch-1U]=src;destination[ctl-1U][ch-1U]=dst;dma_events[ctl-1U][ch-1U]=events;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Abort(unsigned ctl,unsigned ch) { assert(opened[ctl-1U][ch-1U]);++stops;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned ctl,unsigned ch) { assert(opened[ctl-1U][ch-1U]);opened[ctl-1U][ch-1U]=0U;++releases;return ABOX_HAL_OK; }
static void ready(void *context,unsigned id,ABoxHalStatus status)
{ assert(context==(void*)1 && id==1U && status==ABOX_HAL_OK);++notifications; }
int main(void)
{
    uint8_t tx[3]={1,2,3},rx[3]={0};ABoxSpiDmaEvents events={(void*)1,ready};
    assert(ABoxSpi_StartDma(1U,tx,rx,3U,&events)==ABOX_HAL_OK && requests && busy);
    assert(opened[0][1] && opened[0][2] && configs[0][1].direction==ABOX_DMA_PERIPHERAL_TO_MEMORY);
    assert(configs[0][2].direction==ABOX_DMA_MEMORY_TO_PERIPHERAL && source[0][2]==(uintptr_t)tx && destination[0][1]==(uintptr_t)rx);
    assert(ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_BUSY);
    dma_events[0][2]->on_event(dma_events[0][2]->context,1U,3U,ABOX_DMA_COMPLETE);
    assert(!notifications && ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_BUSY);
    dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_COMPLETE);
    assert(notifications==1U && ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_OK);
    assert(!requests && !busy && stops==2U && releases==2U);
    fail_ch=3U;assert(ABoxSpi_StartDma(1U,tx,rx,3U,&events)==ABOX_HAL_BUSY);
    assert(!busy && !opened[0][1] && !requests);fail_ch=0U;
    assert(ABoxSpi_StartDma(1U,0,rx,3U,0)==ABOX_HAL_OK && !configs[0][2].memory_increment);
    dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_TRANSFER_ERROR);
    assert(!requests && ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_ERROR && fault==1U && !busy);
    fault=0U;assert(ABoxSpi_StartDma(1U,tx,0,3U,0)==ABOX_HAL_OK && !configs[0][1].memory_increment);
    assert(ABoxSpi_AbortDma(1U)==ABOX_HAL_OK && fault==1U && !requests);
    fault=0U;assert(ABoxSpi_StartDma(2U,tx,rx,3U,0)==ABOX_HAL_OK && opened[0][3] && opened[0][4]);
    assert(ABoxSpi_AbortDma(2U)==ABOX_HAL_OK);
    fault=0U;assert(ABoxSpi_StartDma(3U,tx,rx,3U,0)==ABOX_HAL_OK && opened[1][0] && opened[1][1]);
    assert(ABoxSpi_AbortDma(3U)==ABOX_HAL_OK);
    fault=0U;dma_directions=ABOX_SPI_DMA_RX;
    assert(ABoxSpi_StartDma(1U,0,rx,3U,&events)==ABOX_HAL_OK && opened[0][1] && !opened[0][2]);
    dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_COMPLETE);
    assert(notifications==2U && halts==1U && ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_OK);
    dma_directions=ABOX_SPI_DMA_TX;
    assert(ABoxSpi_StartDma(1U,tx,0,3U,&events)==ABOX_HAL_OK && !opened[0][1] && opened[0][2]);
    dma_events[0][2]->on_event(dma_events[0][2]->context,1U,3U,ABOX_DMA_COMPLETE);
    assert(notifications==3U && ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_OK);
    dma_directions=ABOX_SPI_DMA_RX;
    assert(ABoxSpi_StartDma(1U,0,rx,3U,0)==ABOX_HAL_OK);
    dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_TRANSFER_ERROR);
    assert(!requests && ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_ERROR && fault==1U);
    fault=0U;dma_directions=ABOX_SPI_DMA_RX|ABOX_SPI_DMA_TX;hw[0].status=3U;
    assert(ABoxSpi_StartDmaCrc(1U,tx,rx,3U,0U,0)==ABOX_HAL_INVALID);
    assert(ABoxSpi_StartDmaCrc(1U,tx,rx,3U,2U,0)==ABOX_HAL_INVALID);
    assert(ABoxSpi_StartDmaCrc(1U,tx,rx,3U,0x107U,0)==ABOX_HAL_UNSUPPORTED && !busy);
    assert(ABoxSpi_StartDmaCrc(1U,tx,rx,3U,7U,0)==ABOX_HAL_OK && hw[0].crc_poly==7U && (hw[0].control1&(1U<<13)));
    dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_COMPLETE);
    dma_events[0][2]->on_event(dma_events[0][2]->context,1U,3U,ABOX_DMA_COMPLETE);
    assert(ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_OK && !(hw[0].control1&(1U<<13)));
    assert(ABoxSpi_StartDmaCrc(1U,tx,rx,3U,7U,0)==ABOX_HAL_OK);
    dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_COMPLETE);
    dma_events[0][2]->on_event(dma_events[0][2]->context,1U,3U,ABOX_DMA_COMPLETE);
    hw[0].status|=1U<<4;
    assert(ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_ERROR && fault && !busy);
    fault=0U;hw[0].status=2U;
    assert(ABoxSpi_StartDmaCrc(1U,tx,rx,3U,7U,0)==ABOX_HAL_OK);
    dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_COMPLETE);
    dma_events[0][2]->on_event(dma_events[0][2]->context,1U,3U,ABOX_DMA_COMPLETE);
    assert(ABoxSpi_FinishDma(1U,3U)==ABOX_HAL_TIMEOUT && fault && !busy);
    fault=0U;dma_directions=ABOX_SPI_DMA_TX;hw[0].status=2U;
    assert(ABoxSpi_StartDmaCrc(1U,tx,0,3U,7U,0)==ABOX_HAL_OK);
    dma_events[0][2]->on_event(dma_events[0][2]->context,1U,3U,ABOX_DMA_COMPLETE);
    assert(ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_OK && !fault && !busy);
    dma_directions=ABOX_SPI_DMA_RX;hw[0].status=3U;
    { unsigned before=halts;
      assert(ABoxSpi_StartDmaCrc(1U,0,rx,3U,7U,0)==ABOX_HAL_OK);
      dma_events[0][1]->on_event(dma_events[0][1]->context,1U,2U,ABOX_DMA_COMPLETE);
      assert(halts==before && ABoxSpi_FinishDma(1U,5U)==ABOX_HAL_OK && halts==before+1U); }
    return 0;
}
