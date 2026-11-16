#ifndef ABOX_SPI_COMMON_H
#define ABOX_SPI_COMMON_H

#include <stdint.h>
#include "abox_spi.h"
#include "abox_platform_port.h"

static inline int ABoxSpi_ConfigValid(unsigned id, const ABoxSpiConfig *c)
{
    return id >= 1U && id <= 3U && c && (unsigned)c->role <= ABOX_SPI_SLAVE &&
           (unsigned)c->nss <= ABOX_SPI_NSS_OUTPUT &&
           (unsigned)c->wiring <= ABOX_SPI_ONE_LINE &&
           !(c->nss == ABOX_SPI_NSS_OUTPUT && c->role != ABOX_SPI_MASTER) &&
           c->prescaler >= 2U && c->prescaler <= 256U &&
           (c->prescaler & (c->prescaler - 1U)) == 0U &&
           (c->bits == 8U || c->bits == 16U) && c->cpol <= 1U &&
           c->cpha <= 1U && c->lsb_first <= 1U;
}

static inline int ABoxSpi_TransferValid(const ABoxSpiConfig *c, const void *tx,
                                        const void *rx, uint16_t frames)
{
    uintptr_t a = (uintptr_t)tx, b = (uintptr_t)rx;
    uint32_t bytes;
    if (!c || !frames || (!tx && !rx) ||
        (c->bits == 16U && ((a | b) & 1U))) return 0;
    if ((c->wiring == ABOX_SPI_TWO_LINE_RX && (tx || !rx)) ||
        (c->wiring == ABOX_SPI_ONE_LINE && (!!tx == !!rx))) return 0;
    bytes = (uint32_t)frames * (c->bits / 8U);
    /* Exact in-place exchange is supported; partial overlap corrupts data. */
    if (tx && rx && a != b && (a < b ? b - a < bytes : a - b < bytes)) return 0;
    return 1;
}

static inline int ABoxSpi_TickReady(void)
{
    const ABoxPlatformPort *p = ABox_PlatformPortGet();
    return p && p->get_tick_ms;
}

#endif
