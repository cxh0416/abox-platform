#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_i2s.h"
#include "abox_i2s_backend.h"
#include "abox_dma.h"
#include "abox_platform_port.h"

static ABoxI2sRegisters hw[2];
static ABoxI2sClockRegisters clock_hw;
static unsigned format_unsupported;
int ABoxI2sHw_FormatSupported(const ABoxI2sConfig *c) { assert(c);return !format_unsupported; }
static unsigned tick,clock_transition=1U;
static uint32_t get_tick(void *context) { (void)context;return tick; }
static ABoxPlatformPort platform_port={.get_tick_ms=get_tick};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return &platform_port; }
uint32_t ABox_PortGetTickMs(void)
{
    if (clock_transition) {
        if (clock_hw.control & (1U<<28)) clock_hw.control|=1U<<29;
        else clock_hw.control &= ~(1U<<29);
    }
    return tick++;
}
ABoxI2sClockRegisters *ABoxI2sHw_ClockRegisters(void) { return &clock_hw; }
uint32_t ABoxI2sHw_ExternalClockHz(void) { return 8000000U; }
static unsigned reserved[2],opened[2][7],last_ctl,last_ch,last_event,event_count,conflict;
static ABoxDmaConfig dma_config;
static const ABoxDmaEvents *dma_events;
static uintptr_t dma_source,dma_dest;
uint32_t ABoxCritical_Enter(void) { return 0U; }
void ABoxCritical_Exit(uint32_t key) { (void)key; }
ABoxHalStatus ABoxI2sHw_Reserve(unsigned id)
{ if (id<2U || id>3U) return ABOX_HAL_INVALID;if(reserved[id-2U])return ABOX_HAL_BUSY;reserved[id-2U]=1U;return ABOX_HAL_OK; }
ABoxI2sRegisters *ABoxI2sHw_Registers(unsigned id) { return &hw[id-2U]; }
void ABoxI2sHw_Release(unsigned id) { assert(reserved[id-2U]);reserved[id-2U]=0U; }
ABoxHalStatus ABoxDma_Open(unsigned ctl,unsigned ch,const ABoxDmaConfig *config)
{
    if(conflict)return ABOX_HAL_BUSY;
    assert(!opened[ctl-1U][ch-1U]);opened[ctl-1U][ch-1U]=1U;dma_config=*config;last_ctl=ctl;last_ch=ch;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxDma_Start(unsigned ctl,unsigned ch,uintptr_t src,uintptr_t dest,uint16_t n,const ABoxDmaEvents *events)
{ assert(opened[ctl-1U][ch-1U] && n);dma_source=src;dma_dest=dest;dma_events=events;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Abort(unsigned ctl,unsigned ch) { assert(opened[ctl-1U][ch-1U]);return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned ctl,unsigned ch) { assert(opened[ctl-1U][ch-1U]);opened[ctl-1U][ch-1U]=0U;return ABOX_HAL_OK; }
static void event(void *context,unsigned id,ABoxI2sEvent type)
{ assert(context==(void *)1 && id>=2U && id<=3U);last_event=(unsigned)type;++event_count; }
static unsigned irq_tx_count,irq_rx_count,irq_error_count,irq_error_event;static uint16_t irq_received;
static uint16_t irq_tx(void *context,unsigned id)
{ assert(context==(void *)2 && id==2U);++irq_tx_count;return 0x1357U; }
static void irq_rx(void *context,unsigned id,uint16_t word)
{ assert(context==(void *)2 && id==2U);++irq_rx_count;irq_received=word; }
static void irq_error(void *context,unsigned id,ABoxI2sEvent event)
{ assert(context==(void *)2 && id==2U);++irq_error_count;irq_error_event=(unsigned)event; }
static void fire(ABoxDmaEvent type) { dma_events->on_event(dma_events->context,last_ctl,last_ch,type); }

int main(void)
{
    ABoxI2sConfig c={ABOX_I2S_MASTER_TX,ABOX_I2S_PHILIPS,ABOX_I2S_16_IN_16,0U,0U,72000000U,48000U,3000U};
    ABoxI2sEvents events={(void *)1,event};uint16_t words[8]={0};uint32_t actual=0U;
    {
        ABoxI2sPllConfig p={8000000U,2U,12U,1U,0U};
        clock_hw.control=1U<<17;
#if defined(ABOX_MCU_GD32F105)
        p.multiplier=15U;assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_OK && actual==120000000U);
        assert((clock_hw.config & (15U<<12))==(13U<<12));
        assert(ABoxI2s_ReleasePll(5U)==ABOX_HAL_OK);p.multiplier=12U;
        p.divider=4U;assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_OK && actual==48000000U);
        assert(ABoxI2s_ReleasePll(5U)==ABOX_HAL_OK);p.divider=2U;
        p.divider=8U;assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_UNSUPPORTED);p.divider=2U;
#else
        p.multiplier=15U;assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_INVALID);p.multiplier=12U;
        p.divider=4U;assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_UNSUPPORTED);p.divider=2U;
#endif
        clock_hw.control|=1U<<26;
        assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_BUSY);
        clock_hw.config=1U<<4;
        assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_OK && actual==96000000U);
        assert((clock_hw.config & (3U<<17))==(1U<<17) && (clock_hw.config & (15U<<12))==(10U<<12));
        assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_BUSY);
        assert(ABoxI2s_ReleasePll(5U)==ABOX_HAL_OK && !(clock_hw.control&(1U<<28)) && !(clock_hw.config&(3U<<17)));
        clock_hw.control=1U<<17;clock_transition=0U;
        assert(ABoxI2s_ConfigurePll(&p,2U,&actual)==ABOX_HAL_TIMEOUT && !(clock_hw.control&(1U<<28)));
        clock_transition=1U;
        assert(ABoxI2s_ConfigurePll(&p,5U,&actual)==ABOX_HAL_OK);
    }
    assert(ABoxI2s_Open(1U,&c,&actual)==ABOX_HAL_INVALID);
    format_unsupported=1U;assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_UNSUPPORTED && !reserved[0]);format_unsupported=0U;
    c.max_error_ppm=2000U;assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_UNSUPPORTED && !reserved[0]);
    c.max_error_ppm=3000U;assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_OK && actual==47872U);
    assert(ABoxI2s_ReleasePll(5U)==ABOX_HAL_BUSY);
    assert(hw[0].i2s_prescaler==(23U|1U<<8U) && hw[0].i2s_config==(1U<<11U|2U<<8U));
    assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_BUSY);
    assert(ABoxI2s_StartDma(2U,words,0,3U,0U,&events)==ABOX_HAL_INVALID);
    assert(ABoxI2s_StartDma(2U,words,0,2U,1U,&events)==ABOX_HAL_INVALID);
    assert(ABoxI2s_StartDma(2U,0,words,4U,1U,&events)==ABOX_HAL_INVALID);
    conflict=1U;assert(ABoxI2s_StartDma(2U,words,0,4U,1U,&events)==ABOX_HAL_BUSY);conflict=0U;
    assert(ABoxI2s_StartDma(2U,words,0,4U,1U,&events)==ABOX_HAL_OK && last_ctl==1U && last_ch==5U);
    assert(dma_config.direction==ABOX_DMA_MEMORY_TO_PERIPHERAL && dma_config.circular &&
           dma_source==(uintptr_t)words && dma_dest==(uintptr_t)&hw[0].data);
    assert((hw[0].control2 & ((1U<<5U)|2U))==((1U<<5U)|2U) && (hw[0].i2s_config & (1U<<10U)));
    assert(ABoxI2s_Close(2U)==ABOX_HAL_BUSY);
    fire(ABOX_DMA_HALF);assert(last_event==ABOX_I2S_HALF);
    fire(ABOX_DMA_COMPLETE);assert(last_event==ABOX_I2S_COMPLETE && (hw[0].i2s_config & (1U<<10U)));
    assert(ABoxI2s_StopDma(2U)==ABOX_HAL_OK && !(hw[0].i2s_config & (1U<<10U)));
    assert(ABoxI2s_StartDma(2U,words,0,2U,0U,&events)==ABOX_HAL_OK);
    { unsigned before=event_count;fire(ABOX_DMA_HALF);assert(event_count==before); }
    fire(ABOX_DMA_COMPLETE);assert(last_event==ABOX_I2S_COMPLETE);
    { unsigned before=event_count;hw[0].status=1U<<3U;ABoxI2s_Irq(2U);assert(event_count==before); }
    assert(ABoxI2s_StopDma(2U)==ABOX_HAL_OK && ABoxI2s_Close(2U)==ABOX_HAL_OK);
    c.mode=ABOX_I2S_SLAVE_RX;c.standard=ABOX_I2S_PCM_LONG;c.format=ABOX_I2S_24_IN_32;
    c.clock_hz=0U;c.sample_hz=0U;c.max_error_ppm=0U;
    assert(ABoxI2s_Open(3U,&c,&actual)==ABOX_HAL_OK && !actual && (hw[1].i2s_config & 0xb3U)==0xb3U);
    assert(ABoxI2s_StartDma(3U,0,words,4U,0U,&events)==ABOX_HAL_OK && last_ctl==2U && last_ch==1U);
    assert(dma_config.direction==ABOX_DMA_PERIPHERAL_TO_MEMORY && dma_source==(uintptr_t)&hw[1].data);
    fire(ABOX_DMA_COMPLETE);assert(!(hw[1].i2s_config & (1U<<10U)));
    assert(ABoxI2s_StopDma(3U)==ABOX_HAL_OK);
    assert(ABoxI2s_StartDma(3U,0,words,8U,1U,&events)==ABOX_HAL_OK);
    hw[1].status=1U<<6U;ABoxI2s_Irq(3U);assert(last_event==ABOX_I2S_OVERRUN);
    assert(ABoxI2s_StopDma(3U)==ABOX_HAL_ERROR && ABoxI2s_StartDma(3U,0,words,8U,1U,&events)==ABOX_HAL_ERROR);
    assert(ABoxI2s_Close(3U)==ABOX_HAL_OK);
    memset(&hw,0,sizeof(hw));c.mode=ABOX_I2S_SLAVE_TX;c.standard=ABOX_I2S_MSB;c.format=ABOX_I2S_16_IN_16;
    assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_OK && ABoxI2s_StartDma(2U,words,0,4U,0U,&events)==ABOX_HAL_OK);
    fire(ABOX_DMA_TRANSFER_ERROR);assert(last_event==ABOX_I2S_DMA_ERROR && ABoxI2s_StopDma(2U)==ABOX_HAL_ERROR);
    assert(ABoxI2s_Close(2U)==ABOX_HAL_OK);
    assert(ABoxI2s_ReleasePll(5U)==ABOX_HAL_OK);
    memset(&hw[0],0,sizeof(hw[0]));
    assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_OK && ABoxI2s_StartDma(2U,words,0,4U,0U,&events)==ABOX_HAL_OK);
    hw[0].status=1U<<3U;fire(ABOX_DMA_COMPLETE);
    assert(last_event==ABOX_I2S_UNDERRUN && ABoxI2s_StopDma(2U)==ABOX_HAL_ERROR);
    assert(ABoxI2s_Close(2U)==ABOX_HAL_OK);
    memset(&hw[0],0,sizeof(hw[0]));words[0]=0x1234U;
    assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_OK);
    assert(ABoxI2s_StartPolling(2U,0)==ABOX_HAL_INVALID);
    assert(ABoxI2s_StartPolling(2U,words)==ABOX_HAL_OK && hw[0].data==0x1234U);
    assert(ABoxI2s_PollWrite(2U,0x5678U)==ABOX_HAL_BUSY && ABoxI2s_Close(2U)==ABOX_HAL_BUSY);
    assert(ABoxI2s_StartDma(2U,words,0,4U,0U,&events)==ABOX_HAL_BUSY);
    hw[0].status=1U<<1U;assert(ABoxI2s_PollWrite(2U,0x5678U)==ABOX_HAL_OK && hw[0].data==0x5678U);
    assert(ABoxI2s_StopPolling(2U)==ABOX_HAL_OK && ABoxI2s_Close(2U)==ABOX_HAL_OK);
    memset(&hw[0],0,sizeof(hw[0]));c.mode=ABOX_I2S_SLAVE_RX;
    assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_OK && ABoxI2s_StartPolling(2U,0)==ABOX_HAL_OK);
    assert(ABoxI2s_PollRead(2U,words)==ABOX_HAL_BUSY);
    hw[0].status=1U;hw[0].data=0xabcdU;
    assert(ABoxI2s_PollRead(2U,words)==ABOX_HAL_OK && words[0]==0xabcdU);
    hw[0].status=1U<<6U;assert(ABoxI2s_PollRead(2U,words)==ABOX_HAL_ERROR);
    assert(ABoxI2s_StopPolling(2U)==ABOX_HAL_ERROR && ABoxI2s_Close(2U)==ABOX_HAL_OK);
    {
        ABoxI2sIrqEvents ie={(void *)2,irq_tx,irq_rx,irq_error};
        memset(&hw[0],0,sizeof(hw[0]));c.mode=ABOX_I2S_SLAVE_TX;words[0]=0x2468U;
        assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_OK && ABoxI2s_StartIrq(2U,words,&ie)==ABOX_HAL_OK);
        assert(hw[0].data==0x2468U && (hw[0].control2 & (1U<<7U)) && ABoxI2s_Close(2U)==ABOX_HAL_BUSY);
        hw[0].status=1U<<1U;ABoxI2s_Irq(2U);assert(irq_tx_count==1U && hw[0].data==0x1357U);
        hw[0].status=1U<<3U;ABoxI2s_Irq(2U);assert(irq_error_count==1U && irq_error_event==ABOX_I2S_UNDERRUN);
        assert(ABoxI2s_StopIrq(2U)==ABOX_HAL_ERROR && ABoxI2s_Close(2U)==ABOX_HAL_OK);
        memset(&hw[0],0,sizeof(hw[0]));c.mode=ABOX_I2S_SLAVE_RX;
        assert(ABoxI2s_Open(2U,&c,&actual)==ABOX_HAL_OK && ABoxI2s_StartIrq(2U,0,&ie)==ABOX_HAL_OK);
        hw[0].status=1U;hw[0].data=0xbabeU;ABoxI2s_Irq(2U);
        assert(irq_rx_count==1U && irq_received==0xbabeU);
        hw[0].status=1U<<6U;assert(ABoxI2s_StopIrq(2U)==ABOX_HAL_ERROR && irq_error_count==1U);
        assert(ABoxI2s_Close(2U)==ABOX_HAL_OK);
    }
    return 0;
}
