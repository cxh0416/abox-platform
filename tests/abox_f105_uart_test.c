#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_uart.h"
#include "abox_f105_uart_hw.h"
#include "abox_dma_resource.h"
#include "abox_platform_port.h"
#include "abox_ec800_rx.h"

static ABoxF105UartRegisters uart[6];
static ABoxF105DmaChannel dma[2][7];
static uint32_t flags[2][7], lock, clock_tick;
static unsigned inject_boundary,irq_during_poll;
ABoxF105UartRegisters *ABoxF105Uart_Registers(unsigned id) { return &uart[id]; }
uint32_t ABoxF105Uart_ClockHz(unsigned id) { return id == 1U ? 72000000U : 36000000U; }
void ABoxF105Uart_EnableClock(unsigned id) { (void)id; }
ABoxF105DmaChannel *ABoxF105Dma_Channel(unsigned c, unsigned n) { return &dma[c-1U][n-1U]; }
void ABoxF105Dma_EnableClock(unsigned c) { (void)c; }
uint32_t ABoxF105Dma_Flags(unsigned c, unsigned n)
{
    if (inject_boundary && --inject_boundary == 0U) {
        assert(c==1U && n==5U); flags[0][4] |= 4U; dma[0][4].count=544U;
    }
    return flags[c-1U][n-1U];
}
void ABoxF105Dma_Clear(unsigned c, unsigned n, uint32_t f) { flags[c-1U][n-1U] &= ~f; }
uint32_t ABoxCritical_Enter(void) { return lock++; }
void ABoxCritical_Exit(uint32_t old) { assert(lock && old == lock-1U); --lock; }
static uint32_t tick(void *unused)
{
    (void)unused;
    if (irq_during_poll) {
        irq_during_poll=0U;uart[1].status=1U<<5U;uart[1].data=0x55U;ABoxUart_Irq(1U);
    }
    return clock_tick++;
}
static ABoxPlatformPort port = {.get_tick_ms = tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return &port; }

static unsigned rx_count, tx_count, err_count, rearm;
static uint16_t positions[16];
static uint8_t *current;
static void on_rx(void *ctx, const uint8_t *buffer, uint16_t size)
{
    unsigned id = (unsigned)(uintptr_t)ctx;
    assert(buffer == current); positions[rx_count++] = size;
    if (rearm) { rearm = 0U; assert(ABoxUart_StartRx(id, ABOX_UART_RX_IDLE, current, 4U)); }
}
static void on_tx(void *ctx) { (void)ctx; ++tx_count; }
static void on_error(void *ctx) { (void)ctx; ++err_count; }
static unsigned breaks;
static void on_break(void *ctx) { assert(ctx==(void *)5U); ++breaks; }
static ABoxUartEvents events = {(void *)1U, on_rx, on_tx, on_error, 0};
static const ABoxUartEvents lin_events = {(void *)5U,on_rx,on_tx,on_error,on_break};
static ABoxEc800Rx ec_rx;
static uint8_t ec_dma[1088], ec_ring[1152], ec_out[1100];
static unsigned ec_size, ec_errors;
static void ec_received(void *context,const uint8_t *data,uint16_t position)
{ (void)context; assert(data==ec_dma && ABoxEc800Rx_OnDmaEvent(&ec_rx,position)); }
static void ec_error(void *context) { (void)context; ++ec_errors; ABoxEc800Rx_RequestRestart(&ec_rx); }
static void ec_sink(void *context,const uint8_t *data,uint16_t size)
{ (void)context; assert(ec_size+size<=sizeof(ec_out));memcpy(ec_out+ec_size,data,size);ec_size+=size; }
static const ABoxUartEvents ec_events = {0,ec_received,0,ec_error,0};
static void dma_event(unsigned c, unsigned n, uint32_t f, unsigned remaining)
{ flags[c-1U][n-1U] = f; dma[c-1U][n-1U].count = remaining; ABoxUart_DmaIrq(c,n); }
int main(void)
{
    ABoxUartConfig c = {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC};
    uint8_t buffer[8] = {0}, tx[] = {1U,2U};
    uint16_t wide[2] = {0U};
    assert(!ABoxUart_Capabilities(0U) && !ABoxUart_Capabilities(6U));
    for (unsigned i=1U; i<=5U; ++i) {
        assert(ABoxUart_Open(i, &c) && ABoxUart_IsConfig(i, &c));
        assert(ABoxUart_Capabilities(i) & ABOX_UART_CAP_RX_FIXED);
        assert(!!(ABoxUart_Capabilities(i) & ABOX_UART_CAP_RX_DMA) == (i != 5U));
    }
    /* Verify fixed physical DMA routes on all four DMA-capable UARTs. */
    {
        const unsigned controllers[]={0U,1U,1U,1U,2U};
        const unsigned rx_channels[]={0U,5U,6U,3U,3U}, tx_channels[]={0U,4U,7U,2U,5U};
        for (unsigned id=2U;id<=4U;++id) {
            unsigned ctl=controllers[id], r=rx_channels[id], t=tx_channels[id];
            assert(ABoxUart_StartRx(id,ABOX_UART_RX_DMA,buffer,8U));
            assert(dma[ctl-1U][r-1U].count==8U && !ABoxDmaResource_Claim(ctl,r));
            assert(ABoxUart_SendDma(id,tx,2U));
            assert(dma[ctl-1U][t-1U].count==2U && !ABoxDmaResource_Claim(ctl,t));
            assert(ABoxUart_AbortRx(id) && ABoxUart_AbortTx(id));
        }
    }
    assert(!ABoxUart_StartRx(5U, ABOX_UART_RX_CIRCULAR_DMA, buffer, sizeof(buffer)));
    assert(!ABoxUart_SendDma(5U, tx, sizeof(tx)));
    assert(ABoxUart_SetEvents(1U, &events)); current = buffer; rearm = 1U;
    assert(ABoxUart_StartRx(1U, ABOX_UART_RX_IDLE, buffer, 4U));
    uart[1].status = (1U<<5); uart[1].data = 42U; ABoxUart_Irq(1U);
    uart[1].status = (1U<<4); ABoxUart_Irq(1U);
    assert(rx_count == 1U && positions[0] == 1U && buffer[0] == 42U);
    uart[1].status = (1U<<5); uart[1].data = 43U; ABoxUart_Irq(1U);
    uart[1].status = (1U<<4); ABoxUart_Irq(1U);
    assert(rx_count == 2U && buffer[0] == 43U);
    assert(ABoxDmaResource_Claim(1U,5U));
    assert(!ABoxUart_StartRx(1U, ABOX_UART_RX_CIRCULAR_DMA, buffer, 8U));
    ABoxDmaResource_Release(1U,5U);
    assert(ABoxUart_StartRx(1U, ABOX_UART_RX_CIRCULAR_DMA, buffer, 8U));
    assert(!ABoxDmaResource_Claim(1U,5U));
    dma_event(1U,5U,4U,4U);
    uart[1].status = (1U<<4); dma[0][4].count = 2U; ABoxUart_Irq(1U);
    dma_event(1U,5U,2U,8U);
    assert(rx_count == 5U && positions[2] == 4U && positions[3] == 6U && positions[4] == 8U);
    assert(ABoxUart_AbortRx(1U));
    assert(ABoxUart_StartRx(1U, ABOX_UART_RX_CIRCULAR_DMA, buffer, 8U));
    dma_event(1U,5U,2U,8U); /* full without half means lost data */
    assert(err_count == 1U && ABoxUart_LastError(1U) == ABOX_UART_ERROR_RX_LOST && ABoxDmaResource_Claim(1U,5U));
    ABoxDmaResource_Release(1U,5U);
    assert(ABoxUart_SendDma(1U, tx, 2U));
    dma_event(1U,4U,2U,0U); assert(tx_count == 0U);
    uart[1].status = (1U<<6); ABoxUart_Irq(1U); assert(tx_count == 1U);
    assert(ABoxUart_Send(1U, tx, 2U));
    uart[1].status = (1U<<7) | (1U<<6); ABoxUart_Irq(1U); assert(uart[1].data == 1U && tx_count == 1U);
    ABoxUart_Irq(1U); assert(uart[1].data == 2U && tx_count == 1U);
    uart[1].status = (1U<<6); ABoxUart_Irq(1U); assert(tx_count == 2U);
    uart[1].status = 0U; clock_tick = 0U;
    assert(!ABoxUart_SendBlocking(1U, tx, 2U, 3U));
    {
        uint16_t received=99U;unsigned old_rx=rx_count,old_errors=err_count;
        assert(ABoxUart_Capabilities(1U)&ABOX_UART_CAP_BLOCKING_RX);
        uart[1].status=0U;clock_tick=0U;irq_during_poll=1U;
        assert(ABoxUart_ReceiveBlocking(1U,buffer,1U,3U,&received) && received==1U && buffer[0]==0x55U);
        assert(rx_count==old_rx && err_count==old_errors);
        uart[1].status=0U;clock_tick=0U;received=99U;
        assert(!ABoxUart_ReceiveBlocking(1U,buffer,1U,2U,&received) && received==0U);
        uart[1].status=(1U<<5U)|(1U<<1U);received=99U;
        assert(!ABoxUart_ReceiveBlocking(1U,buffer,1U,2U,&received) && received==0U &&
               ABoxUart_LastError(1U)==ABOX_UART_ERROR_FRAMING && err_count==old_errors);
    }
    assert(ABoxUart_Close(1U)); c.data_bits = 9U;
    assert(ABoxUart_Open(1U, &c)); current = (uint8_t *)(void *)wide;
    assert(!ABoxUart_StartRx(1U, ABOX_UART_RX_FIXED, (uint8_t *)(void *)wide + 1, 1U));
    assert(ABoxUart_StartRx(1U, ABOX_UART_RX_FIXED, current, 2U));
    uart[1].status = (1U<<5); uart[1].data = 0x1AAU; ABoxUart_Irq(1U);
    uart[1].status = (1U<<5); uart[1].data = 0x155U; ABoxUart_Irq(1U);
    assert(wide[0] == 0x1AAU && wide[1] == 0x155U);
    { uint16_t received=0U;uart[1].status=1U<<5U;uart[1].data=0x1abU;
      assert(ABoxUart_ReceiveBlocking(1U,(uint8_t *)(void *)wide,1U,5U,&received) && received==1U && wide[0]==0x1abU); }
    assert(ABoxUart_Close(1U));
    for (unsigned i=2U; i<=5U; ++i) assert(ABoxUart_Close(i));
    c.data_bits=8U; c.flow=ABOX_UART_FLOW_RTS_CTS; c.stop_bits=2U;
    assert(ABoxUart_Open(3U,&c) && !ABoxUart_Open(4U,&c)); assert(ABoxUart_Close(3U));
    /* Exercise the production modem ring consumer across IDLE, half and wrap. */
    c.flow=ABOX_UART_FLOW_NONE;c.stop_bits=1U;
    assert(ABoxEc800Rx_Init(&ec_rx,ec_dma,sizeof(ec_dma),ec_ring,sizeof(ec_ring)));
    assert(ABoxUart_Open(1U,&c) && ABoxUart_SetEvents(1U,&ec_events));
    assert(ABoxUart_StartRx(1U,ABOX_UART_RX_CIRCULAR_DMA,ec_dma,sizeof(ec_dma)));
    for (unsigned i=0;i<sizeof(ec_dma);++i) ec_dma[i]=(uint8_t)i;
    uart[1].status=(1U<<4);dma[0][4].count=1088U-200U;ABoxUart_Irq(1U);
    dma_event(1U,5U,4U,544U);dma_event(1U,5U,2U,1088U);
    for (unsigned i=0;i<12U;++i) ec_dma[i]=(uint8_t)(1088U+i);
    uart[1].status=(1U<<4);dma[0][4].count=1088U-12U;ABoxUart_Irq(1U);
    assert(ABoxEc800Rx_Drain(&ec_rx,ec_sink,0,0)==1100U && !ec_errors);
    for (unsigned i=0;i<1100U;++i) assert(ec_out[i]==(uint8_t)i);
    assert(ABoxUart_AbortRx(1U));ABoxEc800Rx_AfterRestart(&ec_rx);
    assert(ABoxUart_StartRx(1U,ABOX_UART_RX_CIRCULAR_DMA,ec_dma,sizeof(ec_dma)));
    uart[1].status=(1U<<4);dma[0][4].count=588U;inject_boundary=2U;ABoxUart_Irq(1U);
    assert(ec_rx.dma_position==544U && !ec_errors);
    dma_event(1U,5U,8U,500U);
    assert(ec_errors==1U && ec_rx.restart_requested && ABoxUart_LastError(1U)==ABOX_UART_ERROR_RX_DMA);
    assert(ABoxUart_Close(1U));
    {
        ABoxUartConfig extra={.baud=115200U,.data_bits=8U,.stop_bits=ABOX_UART_STOP_1,
                              .parity=ABOX_UART_PARITY_NONE,.flow=ABOX_UART_FLOW_NONE,
                              .mode=ABOX_UART_SYNC_MASTER,.options.sync={1U,1U,1U}};
        uint8_t detected=0U;
        assert((ABoxUart_Capabilities(1U)&(ABOX_UART_CAP_SYNC_MASTER|ABOX_UART_CAP_SMARTCARD))!=0U);
        assert(!(ABoxUart_Capabilities(5U)&ABOX_UART_CAP_SYNC_MASTER));
        assert(!ABoxUart_Open(4U,&extra));
        assert(ABoxUart_Open(2U,&extra) && ABoxUart_IsConfig(2U,&extra));
        assert((uart[2].control2 & (15U<<8U))==(15U<<8U));
        extra.options.sync.phase=0U;assert(!ABoxUart_IsConfig(2U,&extra));
        assert(ABoxUart_Close(2U));
        memset(&extra.options,0,sizeof(extra.options));extra.mode=ABOX_UART_LIN;extra.options.lin.break_bits=11U;
        assert(ABoxUart_Open(5U,&extra) && ABoxUart_SetEvents(5U,&lin_events));
        assert((uart[5].control2 & ((1U<<14U)|(1U<<6U)|(1U<<5U)))==((1U<<14U)|(1U<<6U)|(1U<<5U)));
        uart[5].status=1U<<6U;assert(ABoxUart_LinSendBreak(5U) && (uart[5].control1&1U));
        uart[5].status=1U<<8U;assert(ABoxUart_LinBreakPending(5U,&detected) && detected);
        ABoxUart_Irq(5U);assert(breaks==1U);
        current=buffer;assert(ABoxUart_StartRx(5U,ABOX_UART_RX_IDLE,buffer,4U));
        uart[5].status=(1U<<8U)|(1U<<1U);ABoxUart_Irq(5U);
        assert(breaks==2U && ABoxUart_LastError(5U)==ABOX_UART_ERROR_FRAMING);
        assert(ABoxUart_Close(5U));
        memset(&extra.options,0,sizeof(extra.options));extra.mode=ABOX_UART_HALF_DUPLEX;
        assert(ABoxUart_Open(5U,&extra) && (uart[5].control3 & (1U<<3U)));
        assert(ABoxUart_StartRx(5U,ABOX_UART_RX_FIXED,buffer,2U));
        assert(!ABoxUart_Send(5U,tx,2U) && ABoxUart_AbortRx(5U));
        assert(ABoxUart_Send(5U,tx,2U) && !ABoxUart_StartRx(5U,ABOX_UART_RX_FIXED,buffer,2U));
        assert(ABoxUart_AbortTx(5U) && ABoxUart_Close(5U));
        extra.mode=ABOX_UART_IRDA;extra.options.irda.low_power=1U;extra.options.irda.prescaler=10U;
        assert(ABoxUart_Open(4U,&extra) && uart[4].guard==10U && (uart[4].control3 & 6U)==6U);
        assert(ABoxUart_Close(4U));extra.options.irda.low_power=0U;assert(!ABoxUart_Open(4U,&extra));
        memset(&extra.options,0,sizeof(extra.options));extra.mode=ABOX_UART_SMARTCARD;
        extra.parity=ABOX_UART_PARITY_EVEN;extra.stop_bits=ABOX_UART_STOP_1_HALF;
        extra.options.smartcard.clock_output=1U;extra.options.smartcard.prescaler=2U;
        extra.options.smartcard.guard_time=10U;extra.options.smartcard.nack=1U;
        assert(!ABoxUart_Open(4U,&extra) && ABoxUart_Open(2U,&extra));
        assert(uart[2].guard==0x0a02U && (uart[2].control3 & (3U<<4U))==(3U<<4U));
        assert(ABoxUart_Send(2U,tx,2U));uart[2].status=1U<<1U;ABoxUart_Irq(2U);
        assert(ABoxUart_LastError(2U)==ABOX_UART_ERROR_FRAMING && ABoxUart_Close(2U));
        extra.stop_bits=ABOX_UART_STOP_HALF;assert(ABoxUart_Open(2U,&extra));
        assert(!ABoxUart_Send(2U,tx,2U));assert(ABoxUart_Close(2U));
        memset(&extra.options,0,sizeof(extra.options));extra.mode=ABOX_UART_MULTIPROCESSOR;
        extra.data_bits=9U;extra.parity=ABOX_UART_PARITY_NONE;extra.stop_bits=ABOX_UART_STOP_1;
        extra.options.multiprocessor.address=10U;extra.options.multiprocessor.wake_on_address=1U;
        assert(ABoxUart_Capabilities(5U)&ABOX_UART_CAP_MULTIPROCESSOR);
        assert(ABoxUart_Open(5U,&extra) && ABoxUart_IsConfig(5U,&extra));
        assert((uart[5].control1 & (1U<<11U)) && (uart[5].control2 & 15U)==10U);
        uart[5].status=1U<<5U;assert(!ABoxUart_SetMute(5U,1U));
        uart[5].status=0U;assert(ABoxUart_SetMute(5U,1U) && ABoxUart_IsMuted(5U,&detected) && detected);
        assert(ABoxUart_IsConfig(5U,&extra));
        extra.options.multiprocessor.wake_on_address=0U;assert(!ABoxUart_IsConfig(5U,&extra));
        assert(ABoxUart_SetMute(5U,0U) && ABoxUart_IsMuted(5U,&detected) && !detected);
        assert(ABoxUart_Close(5U));extra.options.multiprocessor.address=16U;assert(!ABoxUart_Open(5U,&extra));
    }
    assert(lock == 0U);
    return 0;
}
