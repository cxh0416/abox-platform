#ifndef ABOX_SPI_DMA_BACKEND_H
#define ABOX_SPI_DMA_BACKEND_H
#include "abox_spi.h"
#include <stdint.h>
#define ABOX_SPI_DMA_RX 1U
#define ABOX_SPI_DMA_TX 2U
ABoxHalStatus ABoxSpiHw_DmaReserve(unsigned id,const void *tx,void *rx,uint16_t frames,
                                   uint8_t *width,uint8_t *directions);
uintptr_t ABoxSpiHw_DmaAddress(unsigned id);
void ABoxSpiHw_DmaRequest(unsigned id,int enable);
void ABoxSpiHw_DmaHaltReceive(unsigned id);
ABoxHalStatus ABoxSpiHw_DmaIdle(unsigned id);
void ABoxSpiHw_DmaRelease(unsigned id,int fault);
#endif
