#include "abox_adc_common.h"
#include "abox_adc_dma_backend.h"
#include "abox_adc_watch_backend.h"
#include "abox_adc_dual_injected_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_adc.h"

typedef struct { uint8_t open, fault, internal_ready, dma_busy, injected_count; } AdcInstance;
static AdcInstance g_adc[2];
static uint8_t g_open_count, g_divider, g_dual_busy;
static uint32_t adc(unsigned id) { return id == 1U ? ADC0 : id == 2U ? ADC1 : 0U; }
static int wait_clear(uint32_t instance, uint32_t bit, uint32_t timeout_ms)
{
    uint32_t start = ABox_PortGetTickMs();
    while ((ADC_CTL1(instance) & bit) != 0U) {
        if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout_ms) return 0;
    }
    return 1;
}
static void wait_one_ms(void)
{
    uint32_t start = ABox_PortGetTickMs();
    while ((uint32_t)(ABox_PortGetTickMs() - start) < 1U) { }
}

ABoxHalStatus ABoxAdc_Open(unsigned id, const ABoxAdcConfig *config)
{
    AdcInstance *s;
    uint32_t instance = adc(id);
    uint32_t pclk = rcu_clock_freq_get(CK_APB2);
    if (!ABoxAdc_ConfigValid(id, config, pclk,1U) || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s = &g_adc[id - 1U];
    if (g_dual_busy || s->open || (g_open_count && g_divider != config->clock_divider)) return ABOX_HAL_BUSY;
    if (!g_open_count) {
        rcu_periph_clock_enable(RCU_ADC0); /* dual-mode control lives in ADC0 */
        rcu_adc_clock_config(config->clock_divider == 2U ? RCU_CKADC_CKAPB2_DIV2 :
            config->clock_divider == 4U ? RCU_CKADC_CKAPB2_DIV4 :
            config->clock_divider == 6U ? RCU_CKADC_CKAPB2_DIV6 :
            config->clock_divider == 8U ? RCU_CKADC_CKAPB2_DIV8 :
            config->clock_divider == 12U ? RCU_CKADC_CKAPB2_DIV12 : RCU_CKADC_CKAPB2_DIV16);
        adc_mode_config(ADC_MODE_FREE);
    }
    if (id == 2U) rcu_periph_clock_enable(RCU_ADC1);
    adc_deinit(instance);
    adc_data_alignment_config(instance, ADC_DATAALIGN_RIGHT);
    adc_special_function_config(instance, ADC_SCAN_MODE | ADC_CONTINUOUS_MODE, DISABLE);
    adc_channel_length_config(instance, ADC_REGULAR_CHANNEL, 1U);
    adc_external_trigger_source_config(instance, ADC_REGULAR_CHANNEL, ADC0_1_2_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(instance, ADC_REGULAR_CHANNEL, ENABLE);
    adc_enable(instance);
    wait_one_ms();
    /* The pinned vendor calibration helper spins without a timeout. Execute
     * the same register sequence with bounded waits and the platform tick. */
    ADC_CTL1(instance) |= ADC_CTL1_RSTCLB;
    if (!wait_clear(instance, ADC_CTL1_RSTCLB, 10U)) goto calibration_failed;
    ADC_CTL1(instance) |= ADC_CTL1_CLB;
    if (!wait_clear(instance, ADC_CTL1_CLB, 10U)) goto calibration_failed;
    s->open = 1U; s->fault = 0U; s->internal_ready = 0U;
    g_divider = config->clock_divider; ++g_open_count;
    return ABOX_HAL_OK;
calibration_failed:
    adc_disable(instance); adc_deinit(instance);
    if (id == 2U) rcu_periph_clock_disable(RCU_ADC1);
    if (!g_open_count) rcu_periph_clock_disable(RCU_ADC0);
    return ABOX_HAL_TIMEOUT;
}
ABoxHalStatus ABoxAdc_Close(unsigned id)
{
    AdcInstance *s;
    uint32_t instance = adc(id);
    if (!instance) return ABOX_HAL_INVALID;
    s = &g_adc[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count || ABoxAdcWatch_Configured(id)) return ABOX_HAL_BUSY;
    if (id == 1U && s->internal_ready) adc_tempsensor_vrefint_disable();
    adc_disable(instance); adc_deinit(instance);
    s->open = 0U; s->fault = 0U; s->internal_ready = 0U; --g_open_count;
    if (id == 2U) rcu_periph_clock_disable(RCU_ADC1);
    if (!g_open_count) rcu_periph_clock_disable(RCU_ADC0);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_WatchConfigure(unsigned id,const ABoxAdcWatchConfig *config)
{
    AdcInstance *s=&g_adc[id-1U];uint32_t a=adc(id),bits=0U;
    if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    ADC_CTL0(a) &= ~(ADC_CTL0_WDCHSEL|ADC_CTL0_WDEIE|ADC_CTL0_WDSC|ADC_CTL0_IWDEN|ADC_CTL0_RWDEN);
    ADC_WDLT(a)=config->low;ADC_WDHT(a)=config->high;adc_flag_clear(a,ADC_FLAG_WDE);
    if (config->single_channel) bits=ADC_CTL0_WDSC|config->channel;
    if (config->regular) bits|=ADC_CTL0_RWDEN;
    if (config->injected) bits|=ADC_CTL0_IWDEN;
    if (config->interrupt) bits|=ADC_CTL0_WDEIE;
    ADC_CTL0(a) |= bits;return ABOX_HAL_OK;
}
uint8_t ABoxAdcHw_WatchPending(unsigned id) { return adc_flag_get(adc(id),ADC_FLAG_WDE)!=RESET ? 1U : 0U; }
void ABoxAdcHw_WatchClear(unsigned id) { adc_flag_clear(adc(id),ADC_FLAG_WDE); }
void ABoxAdcHw_WatchDisable(unsigned id)
{
    ADC_CTL0(adc(id)) &= ~(ADC_CTL0_WDCHSEL|ADC_CTL0_WDEIE|ADC_CTL0_WDSC|ADC_CTL0_IWDEN|ADC_CTL0_RWDEN);
    ABoxAdcHw_WatchClear(id);
}
ABoxHalStatus ABoxAdc_Read(unsigned id, unsigned channel, ABoxAdcSampleTime sample,
                           uint32_t timeout_ms, uint16_t *value)
{
    AdcInstance *s;
    uint32_t instance = adc(id), start;
    uint16_t sample_value;
    if (!ABoxAdc_ChannelValid(id, channel, sample) || !value || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s = &g_adc[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    if (channel >= 16U && !s->internal_ready) {
        adc_tempsensor_vrefint_enable(); wait_one_ms(); s->internal_ready = 1U;
    }
    adc_regular_channel_config(instance, 0U, (uint8_t)channel, (uint32_t)sample);
    adc_flag_clear(instance, ADC_FLAG_EOC);
    start = ABox_PortGetTickMs();
    adc_software_trigger_enable(instance, ADC_REGULAR_CHANNEL);
    while (adc_flag_get(instance, ADC_FLAG_EOC) == RESET) {
        if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout_ms) {
            adc_disable(instance); s->fault = 1U; return ABOX_HAL_TIMEOUT;
        }
    }
    sample_value = adc_regular_data_read(instance);
    adc_flag_clear(instance, ADC_FLAG_EOC);
    *value = sample_value;
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxAdc_ReadDual(unsigned channel1,unsigned channel2,ABoxAdcSampleTime sample,
                               uint32_t timeout_ms,ABoxAdcDualSample *value)
{
    uint32_t start,packed;int timed_out=0;
    if (!value || channel1==channel2 || !ABoxAdc_TickReady() || !ABoxAdc_ChannelValid(1U,channel1,sample) ||
        !ABoxAdc_ChannelValid(2U,channel2,sample)) return ABOX_HAL_INVALID;
    if (!g_adc[0].open || !g_adc[1].open) return ABOX_HAL_INVALID;
    if (g_dual_busy || g_adc[0].dma_busy || g_adc[1].dma_busy ||
        g_adc[0].injected_count || g_adc[1].injected_count) return ABOX_HAL_BUSY;
    if (g_adc[0].fault || g_adc[1].fault) return ABOX_HAL_ERROR;
    g_dual_busy=1U;adc_disable(ADC0);adc_disable(ADC1);
    adc_mode_config(ADC_DAUL_REGULAL_PARALLEL);
    if (channel1>=16U && !g_adc[0].internal_ready) {
        adc_tempsensor_vrefint_enable();wait_one_ms();g_adc[0].internal_ready=1U;
    }
    adc_regular_channel_config(ADC0,0U,(uint8_t)channel1,(uint32_t)sample);
    adc_regular_channel_config(ADC1,0U,(uint8_t)channel2,(uint32_t)sample);
    adc_flag_clear(ADC0,ADC_FLAG_EOC);adc_flag_clear(ADC1,ADC_FLAG_EOC);
    adc_enable(ADC1);adc_enable(ADC0);wait_one_ms();start=ABox_PortGetTickMs();
    adc_software_trigger_enable(ADC0,ADC_REGULAR_CHANNEL);
    while (adc_flag_get(ADC0,ADC_FLAG_EOC)==RESET) {
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout_ms) { timed_out=1;break; }
    }
    packed=timed_out ? 0U : adc_sync_mode_convert_value_read();
    adc_disable(ADC0);adc_disable(ADC1);adc_mode_config(ADC_MODE_FREE);
    if (!timed_out) { adc_enable(ADC1);adc_enable(ADC0);wait_one_ms(); }
    g_dual_busy=0U;
    if (timed_out) { g_adc[0].fault=1U;g_adc[1].fault=1U;return ABOX_HAL_TIMEOUT; }
    value->adc1=(uint16_t)packed;value->adc2=(uint16_t)(packed>>16U);return ABOX_HAL_OK;
}

ABoxHalStatus ABoxAdc_StartInjected(unsigned id,const ABoxAdcInjectedConfig *config)
{
    AdcInstance *s;uint32_t a=adc(id);
    if (!ABoxAdc_InjectedValid(id,config) || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s=&g_adc[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    adc_disable(a);adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);
    adc_special_function_config(a,ADC_SCAN_MODE,ENABLE);
    adc_channel_length_config(a,ADC_INSERTED_CHANNEL,config->entry_count);
    adc_external_trigger_source_config(a,ADC_INSERTED_CHANNEL,(uint32_t)config->trigger<<12U);
    for (unsigned i=0U;i<config->entry_count;++i) {
        if (config->entries[i].channel>=16U && !s->internal_ready) {
            adc_tempsensor_vrefint_enable();wait_one_ms();s->internal_ready=1U;
        }
        adc_inserted_channel_config(a,(uint8_t)i,config->entries[i].channel,(uint32_t)config->entries[i].sample);
        adc_inserted_channel_offset_config(a,(uint8_t)i,0U);
    }
    adc_flag_clear(a,ADC_FLAG_EOIC);adc_enable(a);wait_one_ms();
    s->injected_count=config->entry_count;
    adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,ENABLE);
    if (config->trigger==ABOX_ADC_INJ_SOFTWARE) adc_software_trigger_enable(a,ADC_INSERTED_CHANNEL);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DualInjectedStart(const ABoxAdcDualInjectedConfig *config)
{
    adc_disable(ADC0);adc_disable(ADC1);
    adc_mode_config(config->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE ? ADC_DAUL_INSERTED_TRIGGER_ROTATION : ADC_DAUL_INSERTED_PARALLEL);
    for (unsigned n=0U;n<2U;++n) {
        uint32_t a=n ? ADC0 : ADC1;
        adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);
        adc_special_function_config(a,ADC_SCAN_MODE,ENABLE);
        adc_channel_length_config(a,ADC_INSERTED_CHANNEL,config->entry_count);
        adc_external_trigger_source_config(a,ADC_INSERTED_CHANNEL,(uint32_t)(n ? config->trigger : ABOX_ADC_INJ_SOFTWARE)<<12U);
        for (unsigned i=0U;i<config->entry_count;++i) {
            uint8_t channel=n ? config->entries[i].adc1_channel : config->entries[i].adc2_channel;
            if (n && channel>=16U && !g_adc[0].internal_ready) {
                adc_tempsensor_vrefint_enable();wait_one_ms();g_adc[0].internal_ready=1U;
            }
            adc_inserted_channel_config(a,(uint8_t)i,channel,(uint32_t)config->entries[i].sample);
            adc_inserted_channel_offset_config(a,(uint8_t)i,0U);
        }
        adc_flag_clear(a,ADC_FLAG_EOIC);
    }
    adc_enable(ADC1);adc_enable(ADC0);wait_one_ms();
    adc_external_trigger_config(ADC1,ADC_INSERTED_CHANNEL,ENABLE);
    adc_external_trigger_config(ADC0,ADC_INSERTED_CHANNEL,ENABLE);
    if (config->trigger==ABOX_ADC_INJ_SOFTWARE) adc_software_trigger_enable(ADC0,ADC_INSERTED_CHANNEL);
    return ABOX_HAL_OK;
}
uint8_t ABoxAdcHw_DualInjectedReady(void)
{ return adc_flag_get(ADC0,ADC_FLAG_EOIC)!=RESET && adc_flag_get(ADC1,ADC_FLAG_EOIC)!=RESET ? 1U : 0U; }
uint8_t ABoxAdcHw_DualInjectedFirstReady(void)
{ return adc_flag_get(ADC0,ADC_FLAG_EOIC)!=RESET ? 1U : 0U; }
void ABoxAdcHw_DualInjectedRead(ABoxAdcDualSample *samples,uint8_t count)
{
    adc_external_trigger_config(ADC0,ADC_INSERTED_CHANNEL,DISABLE);
    for (unsigned i=0U;i<count;++i) {
        samples[i].adc1=adc_inserted_data_read(ADC0,(uint8_t)i);
        samples[i].adc2=adc_inserted_data_read(ADC1,(uint8_t)i);
    }
}
void ABoxAdcHw_DualInjectedStop(void)
{
    adc_external_trigger_config(ADC0,ADC_INSERTED_CHANNEL,DISABLE);
    adc_external_trigger_config(ADC1,ADC_INSERTED_CHANNEL,DISABLE);
    adc_flag_clear(ADC0,ADC_FLAG_EOIC);adc_flag_clear(ADC1,ADC_FLAG_EOIC);
}
uint8_t ABoxAdcHw_DualInjectedFetch(uint8_t count,ABoxAdcDualSample *samples)
{
    if (!(ADC_CTL0(ADC0)&ADC_CTL0_EOICIE) || !ABoxAdcHw_DualInjectedReady()) return 0U;
    for (unsigned i=0U;i<count;++i) {
        samples[i].adc1=adc_inserted_data_read(ADC0,(uint8_t)i);
        samples[i].adc2=adc_inserted_data_read(ADC1,(uint8_t)i);
    }
    adc_flag_clear(ADC0,ADC_FLAG_EOIC);adc_flag_clear(ADC1,ADC_FLAG_EOIC);return 1U;
}
ABoxHalStatus ABoxAdc_InjectedReady(unsigned id,uint8_t *ready)
{
    if (!adc(id) || !ready || !g_adc[id-1U].open || !g_adc[id-1U].injected_count) return ABOX_HAL_INVALID;
    *ready=adc_flag_get(adc(id),ADC_FLAG_EOIC)!=RESET ? 1U : 0U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_FinishInjected(unsigned id,uint32_t timeout_ms,uint16_t *samples,uint8_t capacity)
{
    AdcInstance *s;uint16_t values[4];uint32_t a=adc(id),start;
    if (!a || !samples || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s=&g_adc[id-1U];if (!s->open || !s->injected_count || capacity<s->injected_count) return ABOX_HAL_INVALID;
    start=ABox_PortGetTickMs();
    while (adc_flag_get(a,ADC_FLAG_EOIC)==RESET) {
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout_ms) {
            adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);adc_disable(a);
            s->injected_count=0U;s->fault=1U;return ABOX_HAL_TIMEOUT;
        }
    }
    adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);
    for (unsigned i=0U;i<s->injected_count;++i) values[i]=adc_inserted_data_read(a,(uint8_t)i);
    adc_flag_clear(a,ADC_FLAG_EOIC);adc_disable(a);adc_special_function_config(a,ADC_SCAN_MODE,DISABLE);
    adc_enable(a);wait_one_ms();
    for (unsigned i=0U;i<s->injected_count;++i) samples[i]=values[i];
    s->injected_count=0U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_AbortInjected(unsigned id)
{
    AdcInstance *s;uint32_t a=adc(id);
    if (!a) return ABOX_HAL_INVALID;
    s=&g_adc[id-1U];if (!s->open || !s->injected_count) return ABOX_HAL_INVALID;
    adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);adc_disable(a);
    s->injected_count=0U;s->fault=1U;return ABOX_HAL_OK;
}

ABoxHalStatus ABoxAdcHw_DmaReserve(unsigned id)
{
    AdcInstance *s;
    if (!adc(id)) return ABOX_HAL_INVALID;
    if (id!=1U) return ABOX_HAL_UNSUPPORTED;
    s=&g_adc[id-1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    s->dma_busy=1U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DmaPrepare(unsigned id,const ABoxAdcDmaConfig *config)
{
    AdcInstance *s=&g_adc[id-1U];uint32_t a=adc(id);
    adc_disable(a);adc_external_trigger_config(a,ADC_REGULAR_CHANNEL,DISABLE);
    adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);
    ADC_CTL0(a) &= ~ADC_CTL0_EOICIE;
    adc_special_function_config(a,ADC_INSERTED_CHANNEL_AUTO,DISABLE);
    adc_special_function_config(a,ADC_SCAN_MODE,(config->entry_count>1U || config->linked) ? ENABLE : DISABLE);
    adc_special_function_config(a,ADC_CONTINUOUS_MODE,config->continuous ? ENABLE : DISABLE);
    adc_channel_length_config(a,ADC_REGULAR_CHANNEL,config->entry_count);
    adc_external_trigger_source_config(a,ADC_REGULAR_CHANNEL,(uint32_t)config->trigger<<17U);
    for(unsigned i=0U;i<config->entry_count;++i) {
        if(config->entries[i].channel>=16U && !s->internal_ready) {
            adc_tempsensor_vrefint_enable();wait_one_ms();s->internal_ready=1U;
        }
        adc_regular_channel_config(a,(uint8_t)i,config->entries[i].channel,(uint32_t)config->entries[i].sample);
    }
    if (config->linked) {
        const ABoxAdcLinkedConfig *linked=config->linked;
        adc_channel_length_config(a,ADC_INSERTED_CHANNEL,linked->entry_count);
        adc_external_trigger_source_config(a,ADC_INSERTED_CHANNEL,(uint32_t)linked->trigger<<12U);
        for (unsigned i=0U;i<linked->entry_count;++i) {
            if (linked->entries[i].channel>=16U && !s->internal_ready) {
                adc_tempsensor_vrefint_enable();wait_one_ms();s->internal_ready=1U;
            }
            adc_inserted_channel_config(a,(uint8_t)i,linked->entries[i].channel,(uint32_t)linked->entries[i].sample);
            adc_inserted_channel_offset_config(a,(uint8_t)i,0U);
        }
        adc_special_function_config(a,ADC_INSERTED_CHANNEL_AUTO,linked->mode==ABOX_ADC_INJECT_AFTER_REGULAR ? ENABLE : DISABLE);
    }
    adc_enable(a);wait_one_ms();return ABOX_HAL_OK;
}
uintptr_t ABoxAdcHw_DmaAddress(unsigned id) { return (uintptr_t)&ADC_RDATA(adc(id)); }
ABoxHalStatus ABoxAdcHw_DmaStart(unsigned id,const ABoxAdcDmaConfig *config)
{
    uint32_t a=adc(id);adc_flag_clear(a,ADC_FLAG_EOC);adc_dma_mode_enable(a);
    if (config->linked) {
        adc_flag_clear(a,ADC_FLAG_EOIC);ADC_CTL0(a) |= ADC_CTL0_EOICIE;
        if (config->linked->mode==ABOX_ADC_INJECT_TRIGGERED && config->linked->trigger!=ABOX_ADC_INJ_SOFTWARE)
            adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,ENABLE);
    }
    adc_external_trigger_config(a,ADC_REGULAR_CHANNEL,ENABLE);
    if (config->trigger==ABOX_ADC_TRIG_SOFTWARE) adc_software_trigger_enable(a,ADC_REGULAR_CHANNEL);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DmaTriggerInjected(unsigned id)
{
    uint32_t a=adc(id);adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,ENABLE);
    adc_software_trigger_enable(a,ADC_INSERTED_CHANNEL);return ABOX_HAL_OK;
}
uint8_t ABoxAdcHw_DmaInjectedFetch(unsigned id,uint8_t count,uint16_t *values)
{
    uint32_t a=adc(id);
    if (!(ADC_CTL0(a)&ADC_CTL0_EOICIE) || adc_flag_get(a,ADC_FLAG_EOIC)==RESET) return 0U;
    for (unsigned i=0U;i<count;++i) values[i]=adc_inserted_data_read(a,(uint8_t)i);
    adc_flag_clear(a,ADC_FLAG_EOIC);return 1U;
}
void ABoxAdcHw_DmaInjectedMask(unsigned id)
{ uint32_t a=adc(id);ADC_CTL0(a)&=~ADC_CTL0_EOICIE;adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE); }
void ABoxAdcHw_DmaPause(unsigned id)
{
    uint32_t a=adc(id);adc_dma_mode_disable(a);adc_external_trigger_config(a,ADC_REGULAR_CHANNEL,DISABLE);
    adc_special_function_config(a,ADC_CONTINUOUS_MODE,DISABLE);
}
ABoxHalStatus ABoxAdcHw_DmaRelease(unsigned id,int fault)
{
    AdcInstance *s=&g_adc[id-1U];uint32_t a=adc(id);
    ABoxAdcHw_DmaPause(id);adc_disable(a);
    ABoxAdcHw_DmaInjectedMask(id);adc_flag_clear(a,ADC_FLAG_EOIC);
    adc_special_function_config(a,ADC_INSERTED_CHANNEL_AUTO,DISABLE);
    adc_special_function_config(a,ADC_SCAN_MODE|ADC_CONTINUOUS_MODE,DISABLE);
    adc_channel_length_config(a,ADC_REGULAR_CHANNEL,1U);
    adc_external_trigger_source_config(a,ADC_REGULAR_CHANNEL,ADC0_1_2_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(a,ADC_REGULAR_CHANNEL,ENABLE);adc_enable(a);wait_one_ms();
    s->dma_busy=0U;if(fault)s->fault=1U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DualReserve(void)
{
    if (!g_adc[0].open || !g_adc[1].open) return ABOX_HAL_INVALID;
    if (g_dual_busy || g_adc[0].dma_busy || g_adc[1].dma_busy ||
        g_adc[0].injected_count || g_adc[1].injected_count) return ABOX_HAL_BUSY;
    if (g_adc[0].fault || g_adc[1].fault) return ABOX_HAL_ERROR;
    g_dual_busy=1U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DualPrepare(const ABoxAdcDualDmaConfig *config)
{
    adc_disable(ADC0);adc_disable(ADC1);
    adc_external_trigger_config(ADC0,ADC_REGULAR_CHANNEL,DISABLE);
    adc_external_trigger_config(ADC1,ADC_REGULAR_CHANNEL,DISABLE);
    adc_mode_config(config->linked ? (config->mode==ABOX_ADC_DUAL_INTERLEAVED_FAST ? ADC_DAUL_INSERTED_PARALLEL_REGULAL_FOLLOWUP_FAST :
                    config->mode==ABOX_ADC_DUAL_INTERLEAVED_SLOW ? ADC_DAUL_INSERTED_PARALLEL_REGULAL_FOLLOWUP_SLOW :
                    config->linked->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE ? ADC_DAUL_REGULAL_PARALLEL_INSERTED_ROTATION :
                    ADC_DAUL_REGULAL_PARALLEL_INSERTED_PARALLEL) :
                    (config->mode==ABOX_ADC_DUAL_INTERLEAVED_FAST ? ADC_DAUL_REGULAL_FOLLOWUP_FAST :
                    config->mode==ABOX_ADC_DUAL_INTERLEAVED_SLOW ? ADC_DAUL_REGULAL_FOLLOWUP_SLOW : ADC_DAUL_REGULAL_PARALLEL));
    for (unsigned i=0U;i<2U;++i) {
        uint32_t a=i ? ADC1 : ADC0;
        adc_special_function_config(a,ADC_SCAN_MODE,(config->entry_count>1U || config->linked) ? ENABLE : DISABLE);
        adc_special_function_config(a,ADC_CONTINUOUS_MODE,config->continuous ? ENABLE : DISABLE);
        adc_channel_length_config(a,ADC_REGULAR_CHANNEL,config->entry_count);
        adc_external_trigger_source_config(a,ADC_REGULAR_CHANNEL,i ? ADC0_1_2_EXTTRIG_REGULAR_NONE : (uint32_t)config->trigger<<17U);
    }
    for (unsigned i=0U;i<config->entry_count;++i) {
        const ABoxAdcDualEntry *entry=&config->entries[i];
        if (entry->adc1_channel>=16U && !g_adc[0].internal_ready) {
            adc_tempsensor_vrefint_enable();wait_one_ms();g_adc[0].internal_ready=1U;
        }
        adc_regular_channel_config(ADC0,(uint8_t)i,entry->adc1_channel,(uint32_t)entry->sample);
        adc_regular_channel_config(ADC1,(uint8_t)i,entry->adc2_channel,(uint32_t)entry->sample);
    }
    if (config->linked) {
        const ABoxAdcDualLinkedConfig *linked=config->linked;
        for (unsigned n=0U;n<2U;++n) {
            uint32_t a=n ? ADC0 : ADC1;
            adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);
            adc_special_function_config(a,ADC_INSERTED_CHANNEL_AUTO,linked->automatic ? ENABLE : DISABLE);
            adc_channel_length_config(a,ADC_INSERTED_CHANNEL,linked->entry_count);
            adc_external_trigger_source_config(a,ADC_INSERTED_CHANNEL,(uint32_t)(n ? linked->trigger : ABOX_ADC_INJ_SOFTWARE)<<12U);
            for (unsigned i=0U;i<linked->entry_count;++i) {
                uint8_t ch=n ? linked->entries[i].adc1_channel : linked->entries[i].adc2_channel;
                if (n && ch>=16U && !g_adc[0].internal_ready) {
                    adc_tempsensor_vrefint_enable();wait_one_ms();g_adc[0].internal_ready=1U;
                }
                adc_inserted_channel_config(a,(uint8_t)i,ch,(uint32_t)linked->entries[i].sample);
                adc_inserted_channel_offset_config(a,(uint8_t)i,0U);
            }
            adc_flag_clear(a,ADC_FLAG_EOIC);
        }
    }
    adc_enable(ADC1);adc_enable(ADC0);wait_one_ms();return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DualStart(const ABoxAdcDualDmaConfig *config)
{
    if (config->linked) {
        adc_flag_clear(ADC0,ADC_FLAG_EOIC);adc_flag_clear(ADC1,ADC_FLAG_EOIC);
        ADC_CTL0(ADC0) |= ADC_CTL0_EOICIE;
        if (config->linked->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE) ADC_CTL0(ADC1) |= ADC_CTL0_EOICIE;
        if (!config->linked->automatic) {
            adc_external_trigger_config(ADC1,ADC_INSERTED_CHANNEL,ENABLE);
            if (config->linked->trigger!=ABOX_ADC_INJ_SOFTWARE) adc_external_trigger_config(ADC0,ADC_INSERTED_CHANNEL,ENABLE);
        }
    }
    adc_flag_clear(ADC0,ADC_FLAG_EOC);adc_flag_clear(ADC1,ADC_FLAG_EOC);adc_dma_mode_enable(ADC0);
    adc_external_trigger_config(ADC1,ADC_REGULAR_CHANNEL,ENABLE);
    adc_external_trigger_config(ADC0,ADC_REGULAR_CHANNEL,ENABLE);
    if (config->trigger==ABOX_ADC_TRIG_SOFTWARE) adc_software_trigger_enable(ADC0,ADC_REGULAR_CHANNEL);
    return ABOX_HAL_OK;
}
void ABoxAdcHw_DualRegularPause(void)
{
    adc_dma_mode_disable(ADC0);
    adc_external_trigger_config(ADC0,ADC_REGULAR_CHANNEL,DISABLE);
    adc_external_trigger_config(ADC1,ADC_REGULAR_CHANNEL,DISABLE);
    adc_special_function_config(ADC0,ADC_CONTINUOUS_MODE,DISABLE);
    adc_special_function_config(ADC1,ADC_CONTINUOUS_MODE,DISABLE);
}
void ABoxAdcHw_DualPause(void)
{
    ABoxAdcHw_DualRegularPause();
    /* Slow interleaving remains active with continuous mode disabled. */
    adc_disable(ADC0);adc_disable(ADC1);
}
ABoxHalStatus ABoxAdcHw_DualRelease(int fault)
{
    ABoxAdcHw_DualPause();adc_disable(ADC0);adc_disable(ADC1);adc_mode_config(ADC_MODE_FREE);
    for (unsigned i=0U;i<2U;++i) {
        uint32_t a=i ? ADC1 : ADC0;
        ADC_CTL0(a) &= ~ADC_CTL0_EOICIE;adc_external_trigger_config(a,ADC_INSERTED_CHANNEL,DISABLE);adc_flag_clear(a,ADC_FLAG_EOIC);
        adc_special_function_config(a,ADC_INSERTED_CHANNEL_AUTO,DISABLE);
        adc_special_function_config(a,ADC_SCAN_MODE|ADC_CONTINUOUS_MODE,DISABLE);
        adc_channel_length_config(a,ADC_REGULAR_CHANNEL,1U);
        adc_external_trigger_source_config(a,ADC_REGULAR_CHANNEL,ADC0_1_2_EXTTRIG_REGULAR_NONE);
        adc_external_trigger_config(a,ADC_REGULAR_CHANNEL,ENABLE);adc_enable(a);
    }
    wait_one_ms();g_dual_busy=0U;
    if (fault) { g_adc[0].fault=1U;g_adc[1].fault=1U; }
    return ABOX_HAL_OK;
}
