#include "abox_pinmux.h"
#include "abox_pinmux_backend.h"
#include "abox_critical.h"
#include <stdint.h>

#define DEBUG_MASK (7UL << 24U)
static int debug_bits(ABoxPinMuxDebug debug, uint32_t *bits)
{
    switch (debug) {
    case ABOX_PINMUX_DEBUG_FULL: *bits = 0U; return 1;
    case ABOX_PINMUX_DEBUG_NO_NJTRST: *bits = 1UL << 24U; return 1;
    case ABOX_PINMUX_DEBUG_SWD_ONLY: *bits = 2UL << 24U; return 1;
    case ABOX_PINMUX_DEBUG_DISABLED: *bits = 4UL << 24U; return 1;
    default: return 0;
    }
}
static int route_field(ABoxPinMuxSignal signal, unsigned *shift, unsigned *width, unsigned *valid)
{
    switch (signal) {
    case ABOX_PINMUX_SPI1: *shift=0U; break;
    case ABOX_PINMUX_I2C1: *shift=1U; break;
    case ABOX_PINMUX_UART1: *shift=2U; break;
    case ABOX_PINMUX_UART2: *shift=3U; break;
    case ABOX_PINMUX_UART3: *shift=4U; *width=2U; *valid=0xBU; return 1;
    case ABOX_PINMUX_TIMER1: *shift=6U; *width=2U; *valid=0xBU; return 1;
    case ABOX_PINMUX_TIMER2: *shift=8U; *width=2U; *valid=0xFU; return 1;
    case ABOX_PINMUX_TIMER3: *shift=10U; *width=2U; *valid=0xDU; return 1;
    case ABOX_PINMUX_TIMER4: *shift=12U; break;
    case ABOX_PINMUX_CAN1: *shift=13U; *width=2U; *valid=0xDU; return 1;
    case ABOX_PINMUX_PD01: *shift=15U; break;
    case ABOX_PINMUX_TIMER5_CH4: *shift=16U; break;
    case ABOX_PINMUX_CAN2: *shift=22U; break;
    case ABOX_PINMUX_SPI3: *shift=28U; break;
    case ABOX_PINMUX_TIMER2_ITR1: *shift=29U; break;
    default: return 0;
    }
    *width=1U; *valid=0x3U; return 1;
}
static void update(uint32_t mask, uint32_t value, uint32_t debug)
{
    uint32_t state = ABoxCritical_Enter();
    ABoxPinMuxHw_Enable();
    ABoxPinMuxHw_Write((ABoxPinMuxHw_Read() & ~(mask | DEBUG_MASK)) | value | debug);
    ABoxCritical_Exit(state);
}
int ABoxPinMux_Configure(ABoxPinMuxSignal signal, ABoxPinMuxRoute route, ABoxPinMuxDebug debug)
{
    unsigned shift, width, valid;
    uint32_t bits, mask;
    if (!debug_bits(debug, &bits) || !route_field(signal, &shift, &width, &valid) ||
        (unsigned)route > 3U || !(valid & (1U << (unsigned)route))) return 0;
    mask = ((1UL << width) - 1U) << shift;
    update(mask, (uint32_t)route << shift, bits);
    return 1;
}
int ABoxPinMux_SetDebug(ABoxPinMuxDebug debug)
{
    uint32_t bits;
    if (!debug_bits(debug, &bits)) return 0;
    update(0U, 0U, bits);
    return 1;
}
