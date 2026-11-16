#include "abox_i2s.h"
#include "abox_i2s_backend.h"
#include "abox_dma.h"
#include "abox_critical.h"
#include "abox_platform_port.h"
#include <stddef.h>
#include <stdint.h>

enum { I2S_ENABLE=1U<<10, I2S_SELECT=1U<<11, I2S_RX_DMA=1U<<0, I2S_TX_DMA=1U<<1,
       I2S_ERR_IRQ=1U<<5, I2S_RX_IRQ=1U<<6, I2S_TX_IRQ=1U<<7, I2S_RX_READY=1U<<0, I2S_TX_EMPTY=1U<<1,
       I2S_UNDERRUN_FLAG=1U<<3, I2S_OVERRUN_FLAG=1U<<6 };
typedef struct {
    ABoxI2sRegisters *registers;
    const ABoxI2sEvents *events;
    const ABoxI2sIrqEvents *irq_events;
    ABoxDmaEvents dma_events;
    uint8_t open,mode,frame_words,dma_open,circular,half_aligned,polling,irq_mode;
    volatile uint8_t fault;
} I2sInstance;
static I2sInstance instances[2];
static uint8_t pll_owned;
enum { CLOCK_EXT_READY=1U<<17, CLOCK_OTHER_PLL_ON=1U<<26,
       CLOCK_PLL_ON=1U<<28, CLOCK_PLL_READY=1U<<29,
       CLOCK_DIV_MASK=15U<<4, CLOCK_MUL_MASK=15U<<12,
       CLOCK_I2S2=1U<<17, CLOCK_I2S3=1U<<18 };
static int clock_tick_ready(void)
{ const ABoxPlatformPort *p=ABox_PlatformPortGet();return p && p->get_tick_ms; }
static int wait_clock(ABoxI2sClockRegisters *r,int ready,uint32_t timeout)
{
    uint32_t start=ABox_PortGetTickMs();
    while (!!(r->control & CLOCK_PLL_READY)!=ready)
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout) return 0;
    return 1;
}
ABoxHalStatus ABoxI2s_ConfigurePll(const ABoxI2sPllConfig *c,uint32_t timeout,uint32_t *kernel_hz)
{
    ABoxI2sClockRegisters *r;uint64_t source,pll;uint32_t divider,multiplier,config;
    if (!c || !kernel_hz || !clock_tick_ready() || c->external_hz!=ABoxI2sHw_ExternalClockHz() ||
        !c->divider || c->divider>16U || c->i2s2>1U || c->i2s3>1U || (!c->i2s2 && !c->i2s3)) return ABOX_HAL_INVALID;
#if defined(ABOX_MCU_GD32F105)
    if (!((c->multiplier>=8U && c->multiplier<=16U) || c->multiplier==20U)) return ABOX_HAL_INVALID;
#else
    if (!((c->multiplier>=8U && c->multiplier<=14U) || c->multiplier==16U || c->multiplier==20U)) return ABOX_HAL_INVALID;
#endif
    source=c->external_hz;pll=source*c->multiplier;
#if defined(ABOX_MCU_GD32F105)
    if (source<1000000ULL*c->divider || source>25000000ULL*c->divider ||
        pll<16000000ULL*c->divider || pll>120000000ULL*c->divider) return ABOX_HAL_UNSUPPORTED;
#else
    if (source<3000000ULL*c->divider || source>5000000ULL*c->divider ||
        pll<40000000ULL*c->divider || pll>74000000ULL*c->divider) return ABOX_HAL_UNSUPPORTED;
#endif
    if (pll_owned || instances[0].open || instances[1].open) return ABOX_HAL_BUSY;
    r=ABoxI2sHw_ClockRegisters();if (!r) return ABOX_HAL_ERROR;
    if (!(r->control & CLOCK_EXT_READY) || (r->control & (CLOCK_PLL_ON|CLOCK_PLL_READY))) return ABOX_HAL_BUSY;
    divider=(uint32_t)(c->divider-1U)<<4U;config=r->config;
    if ((r->control&CLOCK_OTHER_PLL_ON) && (config&CLOCK_DIV_MASK)!=divider) return ABOX_HAL_BUSY;
    multiplier=(uint32_t)(c->multiplier==20U ? 15U : c->multiplier-2U)<<12U;
    config=(config & ~(CLOCK_DIV_MASK|CLOCK_MUL_MASK)) | divider | multiplier;
    r->config=config;r->control |= CLOCK_PLL_ON;
    if (!wait_clock(r,1,timeout)) { r->control &= ~CLOCK_PLL_ON;return ABOX_HAL_TIMEOUT; }
    config=r->config & ~(CLOCK_I2S2|CLOCK_I2S3);
    if (c->i2s2) config|=CLOCK_I2S2;
    if (c->i2s3) config|=CLOCK_I2S3;
    r->config=config;pll_owned=1U;*kernel_hz=(uint32_t)((pll*2U)/c->divider);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_ReleasePll(uint32_t timeout)
{
    ABoxI2sClockRegisters *r;
    if (!pll_owned || !clock_tick_ready()) return ABOX_HAL_INVALID;
    if (instances[0].open || instances[1].open) return ABOX_HAL_BUSY;
    r=ABoxI2sHw_ClockRegisters();if (!r) return ABOX_HAL_ERROR;
    r->config &= ~(CLOCK_I2S2|CLOCK_I2S3);r->control &= ~CLOCK_PLL_ON;
    if (!wait_clock(r,0,timeout)) return ABOX_HAL_TIMEOUT;
    pll_owned=0U;return ABOX_HAL_OK;
}
static unsigned controller(unsigned id) { return id==2U ? 1U : 2U; }
static unsigned channel(unsigned id,unsigned mode)
{ return id==2U ? ((mode & 1U) ? 4U : 5U) : ((mode & 1U) ? 1U : 2U); }
static int receiving(unsigned mode) { return (mode & 1U)!=0U; }
static void disable_stream(I2sInstance *s)
{
    s->registers->control2 &= ~(I2S_RX_DMA|I2S_TX_DMA|I2S_ERR_IRQ|I2S_RX_IRQ|I2S_TX_IRQ);
    s->registers->i2s_config &= ~I2S_ENABLE;
}
static void notify(I2sInstance *s,ABoxI2sEvent event)
{
    const ABoxI2sEvents *e=s->events;
    if (e && e->on_event) e->on_event(e->context,(unsigned)(s-instances)+2U,event);
    if (s->irq_mode && s->irq_events && s->irq_events->error)
        s->irq_events->error(s->irq_events->context,(unsigned)(s-instances)+2U,event);
}
static int handle_error(I2sInstance *s)
{
    uint32_t status=s->registers->status;volatile uint32_t discard;
    if (receiving(s->mode) && (status & I2S_OVERRUN_FLAG)) {
        discard=s->registers->data;discard=s->registers->status;(void)discard;
        s->fault=1U;disable_stream(s);notify(s,ABOX_I2S_OVERRUN);return 1;
    }
    if (!receiving(s->mode) && (status & I2S_UNDERRUN_FLAG)) {
        s->fault=1U;disable_stream(s);notify(s,ABOX_I2S_UNDERRUN);return 1;
    }
    return 0;
}
static void dma_event(void *context,unsigned ctl,unsigned ch,ABoxDmaEvent event)
{
    I2sInstance *s=(I2sInstance *)context;uint32_t key=ABoxCritical_Enter();
    (void)ctl;(void)ch;
    if (!s->dma_open || s->fault) { ABoxCritical_Exit(key);return; }
    if (event==ABOX_DMA_TRANSFER_ERROR) {
        s->fault=1U;disable_stream(s);notify(s,ABOX_I2S_DMA_ERROR);
    } else if (event==ABOX_DMA_HALF) {
        if (s->half_aligned) notify(s,ABOX_I2S_HALF);
    } else {
        if (handle_error(s)) { ABoxCritical_Exit(key);return; }
        if (!s->circular) {
            s->registers->control2 &= ~(I2S_RX_DMA|I2S_TX_DMA|I2S_ERR_IRQ);
            if (receiving(s->mode)) s->registers->i2s_config &= ~I2S_ENABLE;
        }
        notify(s,ABOX_I2S_COMPLETE);
    }
    ABoxCritical_Exit(key);
}
ABoxHalStatus ABoxI2s_Open(unsigned id,const ABoxI2sConfig *config,uint32_t *actual_hz)
{
    I2sInstance *s;ABoxI2sRegisters *r;ABoxHalStatus status;
    uint32_t prescaler=2U,actual=0U,settings,format;
    if (id<2U || id>3U || !config || !actual_hz || (unsigned)config->mode>ABOX_I2S_MASTER_RX ||
        (unsigned)config->standard>ABOX_I2S_PCM_LONG || (unsigned)config->format>ABOX_I2S_32_IN_32 ||
        config->clock_polarity>1U || config->master_clock_output>1U) return ABOX_HAL_INVALID;
    if (!ABoxI2sHw_FormatSupported(config)) return ABOX_HAL_UNSUPPORTED;
    if (config->mode<ABOX_I2S_MASTER_TX) {
        if (config->clock_hz || config->sample_hz || config->master_clock_output) return ABOX_HAL_INVALID;
    } else {
        uint64_t divisor,scaled,difference,clock=config->clock_hz;
        uint32_t factor=config->master_clock_output ? 256U : config->format==ABOX_I2S_16_IN_16 ? 32U : 64U;
        if (!config->clock_hz || config->clock_hz>500000000U || !config->sample_hz ||
            config->sample_hz>384000U || config->max_error_ppm>1000000U) return ABOX_HAL_INVALID;
        scaled=(uint64_t)config->sample_hz*factor;
        divisor=(clock+scaled/2U)/scaled;
        if (divisor<4U || divisor>511U) return ABOX_HAL_UNSUPPORTED;
        difference=clock>scaled*divisor ? clock-scaled*divisor : scaled*divisor-clock;
        if (difference*1000000U>scaled*divisor*config->max_error_ppm) return ABOX_HAL_UNSUPPORTED;
        prescaler=(uint32_t)(divisor/2U) | ((uint32_t)(divisor & 1U)<<8U);
        actual=(uint32_t)(clock/(factor*divisor));
    }
    if (config->master_clock_output) prescaler |= 1U<<9U;
    s=&instances[id-2U];if (s->open) return ABOX_HAL_BUSY;
    status=ABoxI2sHw_Reserve(id);if (status!=ABOX_HAL_OK) return status;
    r=ABoxI2sHw_Registers(id);if (!r) { ABoxI2sHw_Release(id);return ABOX_HAL_ERROR; }
    format=config->format==ABOX_I2S_16_IN_16 ? 0U : config->format==ABOX_I2S_16_IN_32 ? 1U :
           config->format==ABOX_I2S_24_IN_32 ? 3U : 5U;
    settings=I2S_SELECT | ((uint32_t)config->mode<<8U) | ((uint32_t)config->clock_polarity<<3U) | format;
    settings |= ((uint32_t)(config->standard==ABOX_I2S_PCM_LONG ? ABOX_I2S_PCM_SHORT : config->standard)<<4U);
    if (config->standard==ABOX_I2S_PCM_LONG) settings |= 1U<<7U;
    r->control1=0U;r->control2=0U;r->i2s_config=0U;r->i2s_prescaler=prescaler;r->i2s_config=settings;
    s->registers=r;s->mode=(uint8_t)config->mode;s->frame_words=config->format>=ABOX_I2S_24_IN_32 ? 4U : 2U;
    s->fault=0U;s->dma_open=0U;s->polling=0U;s->irq_mode=0U;s->irq_events=0;s->open=1U;*actual_hz=actual;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_Close(unsigned id)
{
    I2sInstance *s;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open) return ABOX_HAL_INVALID;
    if (s->dma_open || s->polling || s->irq_mode) return ABOX_HAL_BUSY;
    disable_stream(s);s->registers->i2s_config=0U;s->open=0U;s->events=0;
    ABoxI2sHw_Release(id);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_StartPolling(unsigned id,const uint16_t *first_tx)
{
    I2sInstance *s;volatile uint32_t discard;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open || (receiving(s->mode) ? first_tx!=0 : first_tx==0)) return ABOX_HAL_INVALID;
    if (s->dma_open || s->polling || s->irq_mode) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    discard=s->registers->data;discard=s->registers->status;(void)discard;
    if (!receiving(s->mode)) *(volatile uint16_t *)(void *)&s->registers->data=*first_tx;
    s->events=0;s->polling=1U;s->registers->i2s_config |= I2S_ENABLE;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_PollWrite(unsigned id,uint16_t word)
{
    I2sInstance *s;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open || !s->polling || receiving(s->mode)) return ABOX_HAL_INVALID;
    if (s->fault || handle_error(s)) return ABOX_HAL_ERROR;
    if (!(s->registers->status & I2S_TX_EMPTY)) return ABOX_HAL_BUSY;
    *(volatile uint16_t *)(void *)&s->registers->data=word;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_PollRead(unsigned id,uint16_t *word)
{
    I2sInstance *s;
    if (id<2U || id>3U || !word) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open || !s->polling || !receiving(s->mode)) return ABOX_HAL_INVALID;
    if (s->fault || handle_error(s)) return ABOX_HAL_ERROR;
    if (!(s->registers->status & I2S_RX_READY)) return ABOX_HAL_BUSY;
    *word=*(volatile uint16_t *)(void *)&s->registers->data;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_StopPolling(unsigned id)
{
    I2sInstance *s;uint8_t fault;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open || !s->polling) return ABOX_HAL_INVALID;
    if (!s->fault) (void)handle_error(s);
    fault=s->fault;disable_stream(s);s->polling=0U;return fault ? ABOX_HAL_ERROR : ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_StartIrq(unsigned id,const uint16_t *first_tx,const ABoxI2sIrqEvents *events)
{
    I2sInstance *s;volatile uint32_t discard;uint32_t key;int rx;
    if (id<2U || id>3U || !events) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open) return ABOX_HAL_INVALID;rx=receiving(s->mode);
    if (rx ? (first_tx!=0 || !events->received) : (first_tx==0 || !events->next_tx)) return ABOX_HAL_INVALID;
    if (s->dma_open || s->polling || s->irq_mode) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    key=ABoxCritical_Enter();discard=s->registers->data;discard=s->registers->status;(void)discard;
    if (!rx) *(volatile uint16_t *)(void *)&s->registers->data=*first_tx;
    s->events=0;s->irq_events=events;s->irq_mode=1U;
    s->registers->i2s_config |= I2S_ENABLE;
    s->registers->control2 |= I2S_ERR_IRQ | (rx ? I2S_RX_IRQ : I2S_TX_IRQ);
    ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_StopIrq(unsigned id)
{
    I2sInstance *s;uint32_t key,status;uint8_t fault;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();s=&instances[id-2U];
    if (!s->open || !s->irq_mode) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    status=s->registers->status;
    fault=(uint8_t)(s->fault || (receiving(s->mode) ? !!(status&I2S_OVERRUN_FLAG) : !!(status&I2S_UNDERRUN_FLAG)));
    if (fault) s->fault=1U;
    disable_stream(s);s->irq_mode=0U;s->irq_events=0;ABoxCritical_Exit(key);
    return fault ? ABOX_HAL_ERROR : ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_StartDma(unsigned id,const uint16_t *tx_words,uint16_t *rx_words,
                               uint16_t count,uint8_t circular,const ABoxI2sEvents *events)
{
    I2sInstance *s;ABoxDmaConfig c;ABoxHalStatus status;uintptr_t data;
    unsigned ctl,ch;int rx;volatile uint32_t discard;
    if (id<2U || id>3U || !count || circular>1U) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open || count%s->frame_words ||
        (circular && count%(2U*s->frame_words))) return ABOX_HAL_INVALID;
    if (s->dma_open || s->polling || s->irq_mode) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    ctl=controller(id);ch=channel(id,s->mode);rx=receiving(s->mode);
    if (rx ? (!rx_words || tx_words || ((uintptr_t)rx_words&1U)) :
             (!tx_words || rx_words || ((uintptr_t)tx_words&1U))) return ABOX_HAL_INVALID;
    /* Clear stale receive-overrun and transmit-underrun before arming IRQs. */
    discard=s->registers->data;discard=s->registers->status;(void)discard;
    c.direction=rx ? ABOX_DMA_PERIPHERAL_TO_MEMORY : ABOX_DMA_MEMORY_TO_PERIPHERAL;
    c.peripheral_width=2U;c.memory_width=2U;c.peripheral_increment=0U;c.memory_increment=1U;c.circular=circular;c.priority=2U;
    status=ABoxDma_Open(ctl,ch,&c);if (status!=ABOX_HAL_OK) return status;
    s->dma_open=1U;s->events=events;s->circular=circular;s->half_aligned=(count%(2U*s->frame_words))==0U;
    s->dma_events.context=s;s->dma_events.on_event=dma_event;data=(uintptr_t)&s->registers->data;
    status=ABoxDma_Start(ctl,ch,rx ? data : (uintptr_t)tx_words,rx ? (uintptr_t)rx_words : data,count,&s->dma_events);
    if (status!=ABOX_HAL_OK) {
        ABoxHalStatus closed=ABoxDma_Close(ctl,ch);
        if (closed==ABOX_HAL_OK) { s->dma_open=0U;s->events=0; }
        else s->fault=1U;
        return status;
    }
    s->registers->control2 |= I2S_ERR_IRQ | (rx ? I2S_RX_DMA : I2S_TX_DMA);
    s->registers->i2s_config |= I2S_ENABLE;return ABOX_HAL_OK;
}
static ABoxHalStatus stop_dma(unsigned id,int abort)
{
    I2sInstance *s;ABoxHalStatus status;unsigned ctl,ch;uint8_t fault;
    if (id<2U || id>3U) return ABOX_HAL_INVALID;
    s=&instances[id-2U];if (!s->open || !s->dma_open) return ABOX_HAL_INVALID;
    disable_stream(s);ctl=controller(id);ch=channel(id,s->mode);fault=s->fault;
    status=ABoxDma_Abort(ctl,ch);if (status!=ABOX_HAL_OK) return status;
    status=ABoxDma_Close(ctl,ch);if (status!=ABOX_HAL_OK) return status;
    s->dma_open=0U;s->events=0;if (abort) s->fault=1U;
    return fault ? ABOX_HAL_ERROR : ABOX_HAL_OK;
}
ABoxHalStatus ABoxI2s_StopDma(unsigned id) { return stop_dma(id,0); }
ABoxHalStatus ABoxI2s_AbortDma(unsigned id) { return stop_dma(id,1); }
void ABoxI2s_Irq(unsigned id)
{
    I2sInstance *s;uint32_t key;
    if (id<2U || id>3U) return;
    key=ABoxCritical_Enter();s=&instances[id-2U];
    if (!s->open || (!s->dma_open && !s->irq_mode) || s->fault || !(s->registers->control2 & I2S_ERR_IRQ)) {
        ABoxCritical_Exit(key);return;
    }
    if (!handle_error(s) && s->irq_mode) {
        uint32_t flags=s->registers->status;const ABoxI2sIrqEvents *e=s->irq_events;
        if (receiving(s->mode) && (flags&I2S_RX_READY)) {
            uint16_t word=*(volatile uint16_t *)(void *)&s->registers->data;
            e->received(e->context,id,word);
        } else if (!receiving(s->mode) && (flags&I2S_TX_EMPTY)) {
            uint16_t word=e->next_tx(e->context,id);
            *(volatile uint16_t *)(void *)&s->registers->data=word;
        }
    }
    ABoxCritical_Exit(key);
}
