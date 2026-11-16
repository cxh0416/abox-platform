#include "abox_uart.h"
#include "abox_f105_uart_hw.h"
#include "abox_dma_resource.h"
#include "abox_critical.h"
#include "abox_platform_port.h"
#include <stddef.h>

#ifndef ABOX_UART_ENABLED_MASK
#define ABOX_UART_ENABLED_MASK 0x1FU
#endif
#if (ABOX_UART_ENABLED_MASK & ~0x1FU) || !(ABOX_UART_ENABLED_MASK & 0x1FU)
#error Invalid ABOX_UART_ENABLED_MASK
#endif

/* UART and DMA bit locations are identical on STM32F105 and GD32F105. */
enum {
    U_PE = 1U << 0, U_FE = 1U << 1, U_NE = 1U << 2, U_ORE = 1U << 3,
    U_IDLE = 1U << 4, U_RXNE = 1U << 5, U_TC = 1U << 6, U_TXE = 1U << 7, U_LBD = 1U << 8,
    C_SBK = 1U << 0, C_RWU = 1U << 1,
    C_RE = 1U << 2, C_TE = 1U << 3, C_IDLEIE = 1U << 4, C_RXNEIE = 1U << 5,
    C_TCIE = 1U << 6, C_TXEIE = 1U << 7, C_PEIE = 1U << 8,
    C_PS = 1U << 9, C_PCE = 1U << 10, C_WAKE = 1U << 11, C_M = 1U << 12, C_UE = 1U << 13,
    C_EIE = 1U << 0, C_DMAR = 1U << 6, C_DMAT = 1U << 7,
    C_RTSE = 1U << 8, C_CTSE = 1U << 9,
    C_LBDL = 1U << 5, C_LBDIE = 1U << 6, C_LBCL = 1U << 8,
    C_CPHA = 1U << 9, C_CPOL = 1U << 10, C_CLKEN = 1U << 11, C_LINEN = 1U << 14,
    C_IREN = 1U << 1, C_IRLP = 1U << 2, C_HDSEL = 1U << 3,
    C_NACK = 1U << 4, C_SCEN = 1U << 5,
    D_ENABLE = 1U << 0, D_TCIE = 1U << 1, D_HTIE = 1U << 2,
    D_TEIE = 1U << 3, D_DIR = 1U << 4, D_CIRC = 1U << 5,
    D_MINC = 1U << 7, D_PSIZE16 = 1U << 8, D_MSIZE16 = 1U << 10,
    D_COMPLETE = 1U << 1, D_HALF = 1U << 2, D_ERROR = 1U << 3
};
typedef struct {
    const ABoxUartEvents *events;
    uint8_t *rx;
    const uint8_t *tx;
    uint32_t baud;
    uint16_t rx_size, rx_pos, tx_size, tx_pos;
    uint8_t data_bits, stop_bits, parity, flow, mode;
    volatile uint8_t opened, rx_active, tx_active;
    uint8_t rx_mode, tx_dma, expect_full, rx_epoch, last_error, rx_polling;
} Uart;
#if ABOX_UART_ENABLED_MASK & 1U
static Uart uart1;
#endif
#if ABOX_UART_ENABLED_MASK & 2U
static Uart uart2;
#endif
#if ABOX_UART_ENABLED_MASK & 4U
static Uart uart3;
#endif
#if ABOX_UART_ENABLED_MASK & 8U
static Uart uart4;
#endif
#if ABOX_UART_ENABLED_MASK & 16U
static Uart uart5;
#endif
static Uart *state(unsigned id)
{
    switch (id) {
#if ABOX_UART_ENABLED_MASK & 1U
    case 1U: return &uart1;
#endif
#if ABOX_UART_ENABLED_MASK & 2U
    case 2U: return &uart2;
#endif
#if ABOX_UART_ENABLED_MASK & 4U
    case 3U: return &uart3;
#endif
#if ABOX_UART_ENABLED_MASK & 8U
    case 4U: return &uart4;
#endif
#if ABOX_UART_ENABLED_MASK & 16U
    case 5U: return &uart5;
#endif
    default: return 0;
    }
}
static unsigned dma_controller(unsigned id) { return id == 4U ? 2U : 1U; }
static unsigned dma_rx(unsigned id)
{ static const uint8_t channels[] = {0U, 5U, 6U, 3U, 3U, 0U}; return id <= 5U ? channels[id] : 0U; }
static unsigned dma_tx(unsigned id)
{ static const uint8_t channels[] = {0U, 4U, 7U, 2U, 5U, 0U}; return id <= 5U ? channels[id] : 0U; }
static int dma_rx_mode(unsigned mode)
{ return mode == ABOX_UART_RX_CIRCULAR_DMA || mode == ABOX_UART_RX_DMA || mode == ABOX_UART_RX_IDLE_DMA; }
static int aligned(const Uart *s, const void *buffer)
{ return s->data_bits != 9U || !((uintptr_t)buffer & 1U); }
static uint16_t frame(const Uart *s, const uint8_t *buffer, uint16_t index)
{ return s->data_bits == 9U ? ((const uint16_t *)(const void *)buffer)[index] : buffer[index]; }
static void store_frame(const Uart *s, uint8_t *buffer, uint16_t index, uint16_t value)
{
    if (s->data_bits == 9U) ((uint16_t *)(void *)buffer)[index] = value & 0x1FFU;
    else buffer[index] = (uint8_t)(value & (s->data_bits == 7U ? 0x7FU : 0xFFU));
}
static void emit_error(Uart *s, uint8_t flags)
{ const ABoxUartEvents *e = s->events; s->last_error = flags; if (e && e->error) e->error(e->context); }
static void emit_rx(Uart *s, const uint8_t *data, uint16_t count)
{ const ABoxUartEvents *e = s->events; if (e && e->rx) e->rx(e->context, data, count); }
static void emit_tx(Uart *s)
{ const ABoxUartEvents *e = s->events; if (e && e->tx_complete) e->tx_complete(e->context); }

uint32_t ABoxUart_Capabilities(ABoxUartId id)
{
    uint32_t caps = ABOX_UART_CAP_RX_IDLE | ABOX_UART_CAP_RX_FIXED | ABOX_UART_CAP_ASYNC_TX |
                    ABOX_UART_CAP_BLOCKING_TX | ABOX_UART_CAP_BLOCKING_RX | ABOX_UART_CAP_FLUSH_RX | ABOX_UART_CAP_LIN |
                    ABOX_UART_CAP_HALF_DUPLEX | ABOX_UART_CAP_IRDA | ABOX_UART_CAP_MULTIPROCESSOR;
    if (!state(id)) return 0U;
    if (dma_rx(id)) caps |= ABOX_UART_CAP_RX_CIRCULAR_DMA | ABOX_UART_CAP_RX_DMA | ABOX_UART_CAP_TX_DMA;
    if (id <= 3U) caps |= ABOX_UART_CAP_SYNC_MASTER | ABOX_UART_CAP_SMARTCARD;
    return caps;
}
typedef struct { uint32_t divisor, c1, c2, c3, guard; } UartFormat;
static int format_config(unsigned id,const ABoxUartConfig *config,UartFormat *f)
{
    uint32_t clock, divisor, remainder;
    if (!config || !f || !config->baud || (unsigned)config->parity > ABOX_UART_PARITY_ODD ||
        (unsigned)config->flow > ABOX_UART_FLOW_RTS_CTS || (unsigned)config->mode > ABOX_UART_MULTIPROCESSOR ||
        (config->data_bits != 8U && !(config->data_bits == 7U && config->parity != ABOX_UART_PARITY_NONE) &&
         !(config->data_bits == 9U && config->parity == ABOX_UART_PARITY_NONE)) ||
        config->stop_bits < ABOX_UART_STOP_1 || config->stop_bits > ABOX_UART_STOP_1_HALF ||
        (id > 3U && (config->flow != ABOX_UART_FLOW_NONE || config->stop_bits > ABOX_UART_STOP_2))) return 0;
    clock=ABoxF105Uart_ClockHz(id);
    if (!clock || config->baud > clock) return 0;
    divisor=clock/config->baud;remainder=clock%config->baud;
    if (remainder >= config->baud/2U+config->baud%2U) ++divisor;
    if (divisor < 16U || divisor > 65535U) return 0;
    f->divisor=divisor; f->guard=0U; f->c1=C_RE|C_TE|C_UE; f->c2=0U; f->c3=0U;
    if (config->data_bits==9U || (config->data_bits==8U && config->parity!=ABOX_UART_PARITY_NONE)) f->c1|=C_M;
    if (config->parity!=ABOX_UART_PARITY_NONE) f->c1|=C_PCE;
    if (config->parity==ABOX_UART_PARITY_ODD) f->c1|=C_PS;
    if (config->stop_bits==ABOX_UART_STOP_2) f->c2|=2U<<12U;
    else if (config->stop_bits==ABOX_UART_STOP_HALF) f->c2|=1U<<12U;
    else if (config->stop_bits==ABOX_UART_STOP_1_HALF) f->c2|=3U<<12U;
    if (config->mode!=ABOX_UART_ASYNC && config->mode!=ABOX_UART_MULTIPROCESSOR && config->flow!=ABOX_UART_FLOW_NONE) return 0;
    switch (config->mode) {
    case ABOX_UART_ASYNC:
    case ABOX_UART_MULTIPROCESSOR:
        if (config->flow==ABOX_UART_FLOW_RTS || config->flow==ABOX_UART_FLOW_RTS_CTS) f->c3|=C_RTSE;
        if (config->flow==ABOX_UART_FLOW_CTS || config->flow==ABOX_UART_FLOW_RTS_CTS) f->c3|=C_CTSE;
        if (config->mode==ABOX_UART_MULTIPROCESSOR) {
            if (config->options.multiprocessor.address>15U || config->options.multiprocessor.wake_on_address>1U) return 0;
            f->c2|=config->options.multiprocessor.address;
            if (config->options.multiprocessor.wake_on_address) f->c1|=C_WAKE;
        }
        break;
    case ABOX_UART_SYNC_MASTER:
        if (id>3U || config->options.sync.polarity>1U || config->options.sync.phase>1U ||
            config->options.sync.last_bit_clock>1U) return 0;
        f->c2|=C_CLKEN;
        if (config->options.sync.polarity) f->c2|=C_CPOL;
        if (config->options.sync.phase) f->c2|=C_CPHA;
        if (config->options.sync.last_bit_clock) f->c2|=C_LBCL;
        break;
    case ABOX_UART_LIN:
        if (config->data_bits!=8U || config->parity!=ABOX_UART_PARITY_NONE ||
            config->stop_bits!=ABOX_UART_STOP_1 ||
            (config->options.lin.break_bits!=10U && config->options.lin.break_bits!=11U)) return 0;
        f->c2|=C_LINEN;
        if (config->options.lin.break_bits==11U) f->c2|=C_LBDL;
        break;
    case ABOX_UART_HALF_DUPLEX: f->c3|=C_HDSEL; break;
    case ABOX_UART_IRDA:
        if (config->stop_bits!=ABOX_UART_STOP_1 || config->options.irda.low_power>1U ||
            !config->options.irda.prescaler || (!config->options.irda.low_power && config->options.irda.prescaler!=1U)) return 0;
        f->guard=config->options.irda.prescaler; f->c3|=C_IREN;
        if (config->options.irda.low_power) f->c3|=C_IRLP;
        break;
    case ABOX_UART_SMARTCARD:
        if (id>3U || config->data_bits!=8U || config->parity==ABOX_UART_PARITY_NONE ||
            (config->stop_bits!=ABOX_UART_STOP_HALF && config->stop_bits!=ABOX_UART_STOP_1_HALF) ||
            config->options.smartcard.clock_output>1U || config->options.smartcard.nack>1U ||
            config->options.smartcard.prescaler>31U ||
            (config->options.smartcard.clock_output && !config->options.smartcard.prescaler)) return 0;
        f->guard=((uint32_t)config->options.smartcard.guard_time<<8U)|config->options.smartcard.prescaler;
        f->c3|=C_SCEN;
        if (config->options.smartcard.clock_output) f->c2|=C_CLKEN;
        if (config->options.smartcard.nack) f->c3|=C_NACK;
        break;
    default: return 0;
    }
    return 1;
}
int ABoxUart_Open(ABoxUartId id, const ABoxUartConfig *config)
{
    Uart *s = state(id);
    ABoxF105UartRegisters *u;
    UartFormat f; uint32_t key;
    if (!s || !format_config(id,config,&f)) return 0;
    u = ABoxF105Uart_Registers(id);
    if (!u) return 0;
    key = ABoxCritical_Enter();
    if (s->opened) { ABoxCritical_Exit(key); return 0; }
    ABoxF105Uart_EnableClock(id);
    if (config->mode==ABOX_UART_LIN && s->events && s->events->lin_break) f.c2|=C_LBDIE;
    u->control1 = 0U; u->control2 = f.c2; u->control3 = f.c3; u->guard=f.guard; u->baud = f.divisor; u->control1 = f.c1;
    s->baud = config->baud; s->data_bits = config->data_bits; s->stop_bits = config->stop_bits;
    s->parity = (uint8_t)config->parity; s->flow = (uint8_t)config->flow; s->mode=(uint8_t)config->mode; s->opened = 1U;
    ABoxCritical_Exit(key);
    return 1;
}
int ABoxUart_Close(ABoxUartId id)
{
    Uart *s = state(id); uint32_t key;
    if (!s) return 0;
    key = ABoxCritical_Enter();
    if (!s->opened || s->rx_active || s->tx_active) { ABoxCritical_Exit(key); return 0; }
    ABoxF105Uart_Registers(id)->control1 = 0U; s->opened = 0U;
    ABoxCritical_Exit(key); return 1;
}
int ABoxUart_IsConfig(ABoxUartId id, const ABoxUartConfig *c)
{
    Uart *s = state(id); ABoxF105UartRegisters *u; UartFormat f;
    if (!s || !s->opened || !format_config(id,c,&f)) return 0;
    u=ABoxF105Uart_Registers(id);
    return s->baud==c->baud && s->data_bits==c->data_bits && s->stop_bits==c->stop_bits &&
        s->parity==(unsigned)c->parity && s->flow==(unsigned)c->flow && s->mode==(unsigned)c->mode &&
        u->baud==f.divisor && (u->control1 & (C_RE|C_TE|C_PS|C_PCE|C_WAKE|C_M|C_UE))==f.c1 &&
        (u->control2 & ~C_LBDIE)==f.c2 &&
        (u->control3 & ~(C_EIE|C_DMAR|C_DMAT))==f.c3 && u->guard==f.guard;
}
uint32_t ABoxUart_LastError(ABoxUartId id)
{ Uart *s = state(id); return s ? s->last_error : 0U; }
int ABoxUart_SetEvents(ABoxUartId id, const ABoxUartEvents *events)
{
    Uart *s = state(id); uint32_t key;
    if (!s) return 0;
    key = ABoxCritical_Enter();
    if (s->rx_active || s->tx_active) { ABoxCritical_Exit(key); return 0; }
    s->events = events;
    if (s->opened && s->mode==ABOX_UART_LIN) {
        ABoxF105UartRegisters *u=ABoxF105Uart_Registers(id);
        if (events && events->lin_break) u->control2|=C_LBDIE; else u->control2&=~C_LBDIE;
    }
    ABoxCritical_Exit(key); return 1;
}
static void stop_rx(unsigned id, Uart *s)
{
    ABoxF105UartRegisters *u = ABoxF105Uart_Registers(id);
    u->control1 &= ~(C_IDLEIE | C_RXNEIE | C_PEIE);
    u->control3 &= ~(C_EIE | C_DMAR);
    if (s->rx_active && dma_rx_mode(s->rx_mode)) {
        unsigned controller = dma_controller(id), channel = dma_rx(id);
        ABoxF105Dma_Channel(controller, channel)->control = 0U;
        ABoxF105Dma_Clear(controller, channel, 15U);
        ABoxDmaResource_Release(controller, channel);
    }
    s->rx_active = 0U;
}
int ABoxUart_StartRx(ABoxUartId id, ABoxUartRxMode mode, uint8_t *data, uint16_t length)
{
    Uart *s = state(id); ABoxF105UartRegisters *u; uint32_t key;
    unsigned controller, channel; ABoxF105DmaChannel *d = 0;
    if (!s || !data || !length || (unsigned)mode > ABOX_UART_RX_IDLE_DMA || !aligned(s, data) ||
        (mode == ABOX_UART_RX_CIRCULAR_DMA && (length < 2U || (length & 1U)))) return 0;
    if (dma_rx_mode(mode) && (!dma_rx(id) || s->data_bits == 7U)) return 0;
    key = ABoxCritical_Enter();
    if (!s->opened || s->rx_active ||
        ((s->mode==ABOX_UART_HALF_DUPLEX || s->mode==ABOX_UART_SMARTCARD) && s->tx_active)) {
        ABoxCritical_Exit(key); return 0;
    }
    controller = dma_controller(id); channel = dma_rx(id); u = ABoxF105Uart_Registers(id);
    if (dma_rx_mode(mode)) {
        if (!ABoxDmaResource_Claim(controller, channel)) { ABoxCritical_Exit(key); return 0; }
        ABoxF105Dma_EnableClock(controller); d = ABoxF105Dma_Channel(controller, channel);
        d->control = 0U; ABoxF105Dma_Clear(controller, channel, 15U);
        d->peripheral = (uint32_t)(uintptr_t)&u->data; d->memory = (uint32_t)(uintptr_t)data; d->count = length;
        d->control = D_MINC | D_TCIE | D_TEIE | (s->data_bits == 9U ? D_PSIZE16 | D_MSIZE16 : 0U) |
                     (mode == ABOX_UART_RX_CIRCULAR_DMA ? D_CIRC | D_HTIE : 0U);
    }
    s->rx = data; s->rx_size = length; s->rx_pos = 0U; s->rx_mode = (uint8_t)mode;
    s->expect_full = 0U; ++s->rx_epoch; s->rx_active = 1U;
    if (dma_rx_mode(mode)) { d->control |= D_ENABLE; u->control3 |= C_DMAR; }
    else u->control1 |= C_RXNEIE;
    if (mode == ABOX_UART_RX_IDLE || mode == ABOX_UART_RX_IDLE_DMA || mode == ABOX_UART_RX_CIRCULAR_DMA)
        u->control1 |= C_IDLEIE;
    u->control1 |= C_PEIE; u->control3 |= C_EIE;
    ABoxCritical_Exit(key); return 1;
}
int ABoxUart_AbortRx(ABoxUartId id)
{
    Uart *s = state(id); uint32_t key;
    if (!s) return 0;
    key = ABoxCritical_Enter();
    if (!s->opened || s->rx_polling) { ABoxCritical_Exit(key); return 0; }
    stop_rx(id, s); ABoxCritical_Exit(key); return 1;
}
int ABoxUart_ReceiveBlocking(ABoxUartId id,uint8_t *data,uint16_t length,uint32_t timeout_ms,uint16_t *received)
{
    Uart *s=state(id);const ABoxPlatformPort *p=ABox_PlatformPortGet();ABoxF105UartRegisters *u;
    uint32_t key,start,status;uint16_t count=0U;int ok=0;
    if (!s || !data || !length || !received || !aligned(s,data) || !p || !p->get_tick_ms) return 0;
    key=ABoxCritical_Enter();
    if (!s->opened || s->rx_active ||
        ((s->mode==ABOX_UART_HALF_DUPLEX || s->mode==ABOX_UART_SMARTCARD) && s->tx_active)) {
        ABoxCritical_Exit(key);return 0;
    }
    s->rx_active=1U;s->rx_polling=1U;s->last_error=0U;ABoxCritical_Exit(key);
    u=ABoxF105Uart_Registers(id);start=p->get_tick_ms(p->context);
    while (count<length) {
        status=u->status;
        if (status & (U_PE|U_FE|U_NE|U_ORE)) {
            (void)u->data;s->last_error=(uint8_t)(status & 0xFU);break;
        }
        if (status & U_RXNE) {
            store_frame(s,data,count++,(uint16_t)u->data);
            if (count<length && (uint32_t)(p->get_tick_ms(p->context)-start)>=timeout_ms) break;
            continue;
        }
        if ((uint32_t)(p->get_tick_ms(p->context)-start)>=timeout_ms) break;
    }
    if (count==length) ok=1;
    key=ABoxCritical_Enter();s->rx_polling=0U;s->rx_active=0U;ABoxCritical_Exit(key);
    *received=count;return ok;
}
int ABoxUart_FlushRx(ABoxUartId id)
{
    Uart *s = state(id); ABoxF105UartRegisters *u; uint32_t key;
    if (!s) return 0;
    key = ABoxCritical_Enter();
    if (!s->opened || s->rx_active) { ABoxCritical_Exit(key); return 0; }
    u = ABoxF105Uart_Registers(id); (void)u->status; (void)u->data;
    ABoxCritical_Exit(key); return 1;
}
static void stop_tx(unsigned id, Uart *s)
{
    ABoxF105UartRegisters *u = ABoxF105Uart_Registers(id);
    u->control1 &= ~(C_TXEIE | C_TCIE); u->control3 &= ~C_DMAT;
    if (s->mode==ABOX_UART_SMARTCARD && !s->rx_active) {
        u->control1 &= ~(C_RXNEIE | C_PEIE); u->control3 &= ~C_EIE;
    }
    if (s->tx_active && s->tx_dma) {
        unsigned controller = dma_controller(id), channel = dma_tx(id);
        ABoxF105Dma_Channel(controller, channel)->control = 0U;
        ABoxF105Dma_Clear(controller, channel, 15U); ABoxDmaResource_Release(controller, channel);
    }
    s->tx_active = 0U; s->tx_dma = 0U;
}
static int send(unsigned id, const uint8_t *data, uint16_t length, int use_dma)
{
    Uart *s = state(id); ABoxF105UartRegisters *u; uint32_t key;
    if (!s || !data || !length || !aligned(s, data) || (use_dma && !dma_tx(id))) return 0;
    key = ABoxCritical_Enter();
    if (!s->opened || s->tx_active ||
        ((s->mode==ABOX_UART_HALF_DUPLEX || s->mode==ABOX_UART_SMARTCARD) && s->rx_active) ||
        (s->mode==ABOX_UART_SMARTCARD && s->stop_bits==ABOX_UART_STOP_HALF)) {
        ABoxCritical_Exit(key); return 0;
    }
    u = ABoxF105Uart_Registers(id);
    if (use_dma) {
        unsigned controller = dma_controller(id), channel = dma_tx(id); ABoxF105DmaChannel *d;
        if (!ABoxDmaResource_Claim(controller, channel)) { ABoxCritical_Exit(key); return 0; }
        ABoxF105Dma_EnableClock(controller); d = ABoxF105Dma_Channel(controller, channel);
        d->control = 0U; ABoxF105Dma_Clear(controller, channel, 15U);
        d->peripheral = (uint32_t)(uintptr_t)&u->data; d->memory = (uint32_t)(uintptr_t)data; d->count = length;
        d->control = D_MINC | D_DIR | D_TCIE | D_TEIE | (s->data_bits == 9U ? D_PSIZE16 | D_MSIZE16 : 0U);
    }
    s->tx = data; s->tx_size = length; s->tx_pos = 0U; s->tx_dma = (uint8_t)use_dma; s->tx_active = 1U;
    if (s->mode==ABOX_UART_SMARTCARD) { u->control1 |= C_RXNEIE|C_PEIE; u->control3 |= C_EIE; }
    if (use_dma) {
        u->status = ~U_TC;
        ABoxF105Dma_Channel(dma_controller(id), dma_tx(id))->control |= D_ENABLE;
        u->control3 |= C_DMAT;
    } else u->control1 |= C_TXEIE;
    ABoxCritical_Exit(key); return 1;
}
int ABoxUart_Send(ABoxUartId id, const uint8_t *data, uint16_t length) { return send(id, data, length, 0); }
int ABoxUart_SendDma(ABoxUartId id, const uint8_t *data, uint16_t length) { return send(id, data, length, 1); }
int ABoxUart_AbortTx(ABoxUartId id)
{
    Uart *s = state(id); uint32_t key;
    if (!s) return 0;
    key = ABoxCritical_Enter();
    if (!s->opened) { ABoxCritical_Exit(key); return 0; }
    stop_tx(id, s); ABoxCritical_Exit(key); return 1;
}
int ABoxUart_SendBlocking(ABoxUartId id, const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    Uart *s = state(id); const ABoxPlatformPort *p = ABox_PlatformPortGet();
    ABoxF105UartRegisters *u; uint32_t start, key; uint16_t i;
    if (!s || !data || !length || !aligned(s, data) || !p || !p->get_tick_ms) return 0;
    key = ABoxCritical_Enter();
    if (!s->opened || s->tx_active ||
        ((s->mode==ABOX_UART_HALF_DUPLEX || s->mode==ABOX_UART_SMARTCARD) && s->rx_active) ||
        (s->mode==ABOX_UART_SMARTCARD && s->stop_bits==ABOX_UART_STOP_HALF)) {
        ABoxCritical_Exit(key); return 0;
    }
    s->tx_active = 1U; s->tx_dma = 0U; ABoxCritical_Exit(key);
    u = ABoxF105Uart_Registers(id); start = p->get_tick_ms(p->context);
    for (i = 0U; i < length; ++i) {
        while (!(u->status & U_TXE)) {
            if (s->mode==ABOX_UART_SMARTCARD) {
                uint32_t status=u->status;
                if (status & (U_PE|U_FE|U_NE|U_ORE)) goto card_error;
                if (status & U_RXNE) (void)u->data;
            }
            if ((uint32_t)(p->get_tick_ms(p->context) - start) >= timeout_ms) goto timeout;
        }
        if (s->mode==ABOX_UART_SMARTCARD) {
            uint32_t status=u->status;
            if (status & (U_PE|U_FE|U_NE|U_ORE)) goto card_error;
            if (status & U_RXNE) (void)u->data;
        }
        u->data = frame(s, data, i);
    }
    while (!(u->status & U_TC)) {
        if (s->mode==ABOX_UART_SMARTCARD) {
            uint32_t status=u->status;
            if (status & (U_PE|U_FE|U_NE|U_ORE)) goto card_error;
            if (status & U_RXNE) (void)u->data;
        }
        if ((uint32_t)(p->get_tick_ms(p->context) - start) >= timeout_ms) goto timeout;
    }
    if (s->mode==ABOX_UART_SMARTCARD && (u->status & (U_PE|U_FE|U_NE|U_ORE))) goto card_error;
    key = ABoxCritical_Enter(); s->tx_active = 0U; ABoxCritical_Exit(key); return 1;
card_error:
    s->last_error=(uint8_t)(u->status & 0xFU); (void)u->data;
timeout:
    key = ABoxCritical_Enter(); s->tx_active = 0U; ABoxCritical_Exit(key); return 0;
}
int ABoxUart_LinSendBreak(ABoxUartId id)
{
    Uart *s=state(id);ABoxF105UartRegisters *u;uint32_t key;
    if(!s)return 0;
    key=ABoxCritical_Enter();u=ABoxF105Uart_Registers(id);
    if(!s->opened || s->mode!=ABOX_UART_LIN || s->tx_active || !(u->status & U_TC)) {
        ABoxCritical_Exit(key);return 0;
    }
    u->control1 |= C_SBK;ABoxCritical_Exit(key);return 1;
}
int ABoxUart_LinBreakPending(ABoxUartId id,uint8_t *pending)
{
    Uart *s=state(id);
    if(!s || !s->opened || s->mode!=ABOX_UART_LIN || !pending)return 0;
    *pending=(ABoxF105Uart_Registers(id)->status & U_LBD) ? 1U : 0U;return 1;
}
int ABoxUart_LinClearBreak(ABoxUartId id)
{
    Uart *s=state(id);
    if(!s || !s->opened || s->mode!=ABOX_UART_LIN)return 0;
    ABoxF105Uart_Registers(id)->status=~U_LBD;return 1;
}
int ABoxUart_SetMute(ABoxUartId id,uint8_t mute)
{
    Uart *s=state(id);ABoxF105UartRegisters *u;uint32_t key;
    if (!s || mute>1U) return 0;
    key=ABoxCritical_Enter();u=ABoxF105Uart_Registers(id);
    if (!s->opened || s->mode!=ABOX_UART_MULTIPROCESSOR || (mute && (u->status & U_RXNE))) {
        ABoxCritical_Exit(key);return 0;
    }
    if (mute) u->control1|=C_RWU;else u->control1&=~C_RWU;
    ABoxCritical_Exit(key);return 1;
}
int ABoxUart_IsMuted(ABoxUartId id,uint8_t *mute)
{
    Uart *s=state(id);
    if (!s || !s->opened || s->mode!=ABOX_UART_MULTIPROCESSOR || !mute) return 0;
    *mute=(ABoxF105Uart_Registers(id)->control1 & C_RWU) ? 1U : 0U;return 1;
}
static void finish_rx(unsigned id, Uart *s, uint16_t count)
{
    const uint8_t *buffer = s->rx;
    stop_rx(id, s);
    emit_rx(s, buffer, count); /* the callback may start the next one-shot RX */
}
static int circular_pending(unsigned id, Uart *s)
{
    unsigned controller = dma_controller(id), channel = dma_rx(id);
    uint32_t flags = ABoxF105Dma_Flags(controller, channel);
    ABoxF105DmaChannel *d = ABoxF105Dma_Channel(controller, channel);
    if ((flags & D_ERROR) || ((flags & (D_HALF | D_COMPLETE)) == (D_HALF | D_COMPLETE) &&
        !s->expect_full && d->count <= s->rx_size / 2U)) goto error;
    if (flags & D_HALF) {
        ABoxF105Dma_Clear(controller, channel, D_HALF);
        if (s->expect_full || s->rx_pos > s->rx_size / 2U) goto error;
        s->expect_full = 1U; s->rx_pos = s->rx_size / 2U; emit_rx(s, s->rx, s->rx_pos);
        if (!s->rx_active) return 0;
    }
    if (flags & D_COMPLETE) {
        ABoxF105Dma_Clear(controller, channel, D_COMPLETE);
        if (!s->expect_full) goto error;
        s->expect_full = 0U; s->rx_pos = 0U; emit_rx(s, s->rx, s->rx_size);
        if (!s->rx_active) return 0;
    }
    return 1;
error:
    stop_rx(id, s); emit_error(s, (flags & D_ERROR) ? ABOX_UART_ERROR_RX_DMA : ABOX_UART_ERROR_RX_LOST); return 0;
}
static void dma_irq(unsigned id, unsigned channel)
{
    Uart *s = state(id); unsigned controller = dma_controller(id); uint32_t flags;
    if (!s || !s->opened) return;
    flags = ABoxF105Dma_Flags(controller, channel);
    if (channel == dma_rx(id) && s->rx_active && dma_rx_mode(s->rx_mode)) {
        if (s->rx_mode == ABOX_UART_RX_CIRCULAR_DMA) { (void)circular_pending(id, s); return; }
        if (flags & D_ERROR) { stop_rx(id, s); emit_error(s, ABOX_UART_ERROR_RX_DMA); }
        else if (flags & D_COMPLETE) finish_rx(id, s, s->rx_size);
    } else if (channel == dma_tx(id) && s->tx_active && s->tx_dma) {
        if (flags & D_ERROR) { stop_tx(id, s); emit_error(s, ABOX_UART_ERROR_TX_DMA); }
        else if (flags & D_COMPLETE) {
            ABoxF105UartRegisters *u = ABoxF105Uart_Registers(id);
            u->control3 &= ~C_DMAT;
            ABoxF105Dma_Channel(controller, channel)->control = 0U;
            ABoxF105Dma_Clear(controller, channel, 15U);
            ABoxDmaResource_Release(controller, channel); s->tx_dma = 0U;
            u->control1 |= C_TCIE;
        }
    }
}
void ABoxUart_DmaIrq(unsigned controller, unsigned channel)
{
    uint32_t key = ABoxCritical_Enter();
    for (unsigned id = 1U; id <= 4U; ++id)
        if (controller == dma_controller(id) && (channel == dma_rx(id) || channel == dma_tx(id))) dma_irq(id, channel);
    ABoxCritical_Exit(key);
}
void ABoxUart_Irq(ABoxUartId id)
{
    Uart *s = state(id); ABoxF105UartRegisters *u; uint32_t status, key;
    if (!s) return;
    key = ABoxCritical_Enter();
    if (!s->opened) { ABoxCritical_Exit(key); return; }
    u = ABoxF105Uart_Registers(id); status = u->status;
    if (s->mode==ABOX_UART_LIN && (status & U_LBD) && (u->control2 & C_LBDIE)) {
        const ABoxUartEvents *events=s->events;
        u->status=~U_LBD;
        if ((status & U_FE) && !s->rx_polling) {
            (void)u->data;
            if (s->rx_active) { stop_rx(id,s); emit_error(s,ABOX_UART_ERROR_FRAMING); }
            status &= ~(U_FE|U_RXNE|U_IDLE);
        }
        if (events && events->lin_break) events->lin_break(events->context);
    }
    if (s->mode==ABOX_UART_SMARTCARD && s->tx_active && (status & (U_PE|U_FE|U_NE|U_ORE))) {
        (void)u->data; stop_tx(id,s); emit_error(s,(uint8_t)(status & 0xFU));
        status &= ~(U_PE|U_FE|U_NE|U_ORE|U_RXNE);
    } else if (s->mode==ABOX_UART_SMARTCARD && s->tx_active && !s->rx_active && (status & U_RXNE)) {
        (void)u->data; status &= ~U_RXNE;
    }
    if (s->rx_active && !s->rx_polling && (status & (U_PE | U_FE | U_NE | U_ORE))) {
        (void)u->data; stop_rx(id, s); emit_error(s, (uint8_t)(status & 0xFU));
    } else if (s->rx_active && !s->rx_polling && dma_rx_mode(s->rx_mode) && (status & U_IDLE)) {
        unsigned controller = dma_controller(id), channel = dma_rx(id), attempt;
        uint32_t remaining;
        (void)u->data; /* status then data clears IDLE */
        if (status & U_RXNE) { stop_rx(id, s); emit_error(s, ABOX_UART_ERROR_RX_LOST); goto tx; }
        if (s->rx_mode == ABOX_UART_RX_CIRCULAR_DMA) {
            for (attempt = 0U; attempt < 3U; ++attempt) {
                if (!circular_pending(id, s)) goto tx;
                remaining = ABoxF105Dma_Channel(controller, channel)->count;
                if (!(ABoxF105Dma_Flags(controller, channel) & (D_HALF | D_COMPLETE | D_ERROR))) break;
            }
            if (attempt == 3U) { stop_rx(id, s); emit_error(s, ABOX_UART_ERROR_RX_LOST); goto tx; }
            if (remaining && remaining < s->rx_size) {
                uint16_t position = (uint16_t)(s->rx_size - remaining);
                if (position < s->rx_pos) { stop_rx(id, s); emit_error(s, ABOX_UART_ERROR_RX_LOST); }
                else if (position > s->rx_pos) { s->rx_pos = position; emit_rx(s, s->rx, position); }
            }
        } else if (s->rx_mode == ABOX_UART_RX_IDLE_DMA) {
            uint8_t epoch = s->rx_epoch;
            dma_irq(id, channel);
            if (s->rx_active && s->rx_epoch == epoch) {
                remaining = ABoxF105Dma_Channel(controller, channel)->count;
                if (remaining < s->rx_size) finish_rx(id, s, (uint16_t)(s->rx_size - remaining));
            }
        }
    } else if (s->rx_active && !s->rx_polling && !dma_rx_mode(s->rx_mode)) {
        if (status & U_RXNE) {
            store_frame(s, s->rx, s->rx_pos++, (uint16_t)u->data);
            if (s->rx_pos == s->rx_size) { finish_rx(id, s, s->rx_pos); goto tx; }
        }
        if ((status & U_IDLE) && s->rx_mode == ABOX_UART_RX_IDLE) {
            if (!(status & U_RXNE)) (void)u->data;
            if (s->rx_pos) finish_rx(id, s, s->rx_pos);
        }
    }
tx:
    if (s->tx_active && !s->tx_dma && (u->control1 & C_TXEIE) && (status & U_TXE)) {
        u->data = frame(s, s->tx, s->tx_pos++);
        if (s->tx_pos == s->tx_size) { u->control1 &= ~C_TXEIE; u->control1 |= C_TCIE; }
        ABoxCritical_Exit(key); return;
    }
    if (s->tx_active && !s->tx_dma && (u->control1 & C_TCIE) && (status & U_TC)) {
        stop_tx(id,s); emit_tx(s);
    }
    ABoxCritical_Exit(key);
}
