#include "abox_adc_common.h"
#include "abox_adc_dma_backend.h"
#include "abox_adc_dual_injected_backend.h"
#include "abox_dma.h"
#include "abox_critical.h"
typedef struct {
    const ABoxAdcDmaEvents *events; ABoxDmaEvents dma_events;
    uint8_t owned, dma_open, circular, dual;
    const ABoxAdcLinkedEvents *linked_events;
    const ABoxAdcDualLinkedEvents *dual_linked_events;
    uint16_t latest[4],expected_injected;
    ABoxAdcDualSample dual_latest[4], dual_partial[4];
    volatile uint32_t generation;
    volatile uint16_t observed_injected;
    volatile uint8_t fault,complete,linked_count,linked_auto,linked_software,manual_pending,linked_alternate,dual_parts;
} AdcDma;
static AdcDma transfer;
static void on_dma(void *context,unsigned controller,unsigned channel,ABoxDmaEvent event)
{
    AdcDma *s=(AdcDma *)context;const ABoxAdcDmaEvents *events=s->events;
    (void)controller;(void)channel;
    if (event==ABOX_DMA_TRANSFER_ERROR) {
        s->fault=1U;
        if (s->linked_count) { ABoxAdcHw_DmaInjectedMask(1U);if(s->dual && s->linked_alternate)ABoxAdcHw_DmaInjectedMask(2U); }
    }
    if (event==ABOX_DMA_TRANSFER_ERROR || (event==ABOX_DMA_COMPLETE && !s->circular)) {
        if (s->dual && s->linked_count && event==ABOX_DMA_COMPLETE) ABoxAdcHw_DualRegularPause();
        else if (s->dual) ABoxAdcHw_DualPause(); else ABoxAdcHw_DmaPause(1U);
        if (event==ABOX_DMA_COMPLETE) s->complete=1U;
    }
    if (events && events->on_event) events->on_event(events->context,event);
}
static ABoxHalStatus cleanup(unsigned id)
{
    ABoxHalStatus status;uint32_t key;
    key=ABoxCritical_Enter();
    if (transfer.linked_count) { ABoxAdcHw_DmaInjectedMask(id);if(transfer.dual && transfer.linked_alternate)ABoxAdcHw_DmaInjectedMask(2U); }
    transfer.linked_count=0U;transfer.linked_events=0;transfer.dual_linked_events=0;transfer.manual_pending=0U;
    ABoxCritical_Exit(key);
    if (transfer.dual) ABoxAdcHw_DualPause(); else ABoxAdcHw_DmaPause(id);
    if (transfer.dma_open) {
        status=ABoxDma_Abort(1U,1U);if(status!=ABOX_HAL_OK)return status;
        status=ABoxDma_Close(1U,1U);if(status!=ABOX_HAL_OK)return status;
        transfer.dma_open=0U;
    }
    status=transfer.dual ? ABoxAdcHw_DualRelease(transfer.fault) : ABoxAdcHw_DmaRelease(id,transfer.fault);
    transfer.owned=0U;transfer.events=0;transfer.dual=0U;
    return status;
}
ABoxHalStatus ABoxAdc_StartDma(unsigned id,const ABoxAdcDmaConfig *config,
                               uint16_t *samples,uint16_t count,const ABoxAdcDmaEvents *events)
{
    ABoxHalStatus status;ABoxDmaConfig dma={ABOX_DMA_PERIPHERAL_TO_MEMORY,2U,2U,0U,1U,0U,1U};
    if (!ABoxAdc_TickReady() || id<1U || id>2U || !config || !config->entries || !config->entry_count || config->entry_count>16U ||
        (unsigned)config->trigger>ABOX_ADC_TRIG_SOFTWARE || config->continuous>1U || config->circular>1U ||
        !samples || ((uintptr_t)samples&1U) || !count || count%config->entry_count ||
        (config->continuous && config->trigger!=ABOX_ADC_TRIG_SOFTWARE) ||
        (config->circular && count%(2U*config->entry_count)) ||
        (config->trigger==ABOX_ADC_TRIG_SOFTWARE && !config->continuous && count!=config->entry_count))
        return ABOX_HAL_INVALID;
    for(unsigned i=0U;i<config->entry_count;++i) {
        if (!ABoxAdc_ChannelValid(id,config->entries[i].channel,config->entries[i].sample)) return ABOX_HAL_INVALID;
        for(unsigned j=0U;j<i;++j)
            if(config->entries[i].channel==config->entries[j].channel && config->entries[i].sample!=config->entries[j].sample)
                return ABOX_HAL_INVALID;
    }
    if (config->linked) {
        const ABoxAdcLinkedConfig *linked=config->linked;
        if (!linked->entries || !linked->entry_count || linked->entry_count>4U ||
            (unsigned)linked->mode>ABOX_ADC_INJECT_TRIGGERED || (unsigned)linked->trigger>ABOX_ADC_INJ_SOFTWARE ||
            (linked->mode==ABOX_ADC_INJECT_AFTER_REGULAR && linked->trigger!=ABOX_ADC_INJ_SOFTWARE)) return ABOX_HAL_INVALID;
        for (unsigned i=0U;i<linked->entry_count;++i) {
            const ABoxAdcSequenceEntry *entry=&linked->entries[i];
            if (!ABoxAdc_ChannelValid(id,entry->channel,entry->sample)) return ABOX_HAL_INVALID;
            for (unsigned j=0U;j<i;++j)
                if (entry->channel==linked->entries[j].channel && entry->sample!=linked->entries[j].sample) return ABOX_HAL_INVALID;
            for (unsigned j=0U;j<config->entry_count;++j)
                if (entry->channel==config->entries[j].channel && entry->sample!=config->entries[j].sample) return ABOX_HAL_INVALID;
        }
    }
    if (transfer.owned) return ABOX_HAL_BUSY;
    status=ABoxAdcHw_DmaReserve(id);if(status!=ABOX_HAL_OK)return status;
    transfer.owned=1U;transfer.fault=0U;transfer.complete=0U;transfer.dual=0U;transfer.circular=config->circular;transfer.events=events;
    transfer.generation=0U;transfer.observed_injected=0U;transfer.manual_pending=0U;
    transfer.linked_count=config->linked ? config->linked->entry_count : 0U;
    transfer.linked_auto=config->linked && config->linked->mode==ABOX_ADC_INJECT_AFTER_REGULAR;
    transfer.linked_software=config->linked && config->linked->mode==ABOX_ADC_INJECT_TRIGGERED &&
                             config->linked->trigger==ABOX_ADC_INJ_SOFTWARE;
    transfer.expected_injected=transfer.linked_auto ? (uint16_t)(count/config->entry_count) : 0U;
    transfer.linked_events=config->linked ? config->linked->events : 0;
    transfer.dma_events.context=&transfer;transfer.dma_events.on_event=on_dma;dma.circular=config->circular;
    status=ABoxDma_Open(1U,1U,&dma);if(status!=ABOX_HAL_OK)goto failed;
    transfer.dma_open=1U;
    status=ABoxAdcHw_DmaPrepare(id,config);if(status!=ABOX_HAL_OK){transfer.fault=1U;goto failed;}
    status=ABoxDma_Start(1U,1U,ABoxAdcHw_DmaAddress(id),(uintptr_t)samples,count,&transfer.dma_events);
    if(status!=ABOX_HAL_OK)goto failed;
    status=ABoxAdcHw_DmaStart(id,config);if(status!=ABOX_HAL_OK){transfer.fault=1U;goto failed;}
    return ABOX_HAL_OK;
failed:
    (void)cleanup(id);return status;
}
ABoxHalStatus ABoxAdc_StopDma(unsigned id)
{
    if (id!=1U || !transfer.owned || transfer.dual) return ABOX_HAL_INVALID;
    if (!transfer.fault && !transfer.circular && transfer.complete && transfer.linked_auto &&
        transfer.observed_injected<transfer.expected_injected) return ABOX_HAL_BUSY;
    return cleanup(id);
}
ABoxHalStatus ABoxAdc_AbortDma(unsigned id)
{
    if (id!=1U || !transfer.owned || transfer.dual) return ABOX_HAL_INVALID;
    transfer.fault=1U;return cleanup(id);
}
ABoxHalStatus ABoxAdc_TriggerLinkedInjected(unsigned id)
{
    uint32_t key;ABoxHalStatus status;
    if (id!=1U) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();
    if (!transfer.owned || transfer.dual || !transfer.linked_count || !transfer.linked_software || transfer.fault) {
        ABoxCritical_Exit(key);return ABOX_HAL_INVALID;
    }
    if (transfer.manual_pending) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    transfer.manual_pending=1U;status=ABoxAdcHw_DmaTriggerInjected(id);
    if (status!=ABOX_HAL_OK) transfer.manual_pending=0U;
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxAdc_GetLinkedInjected(unsigned id,uint16_t *samples,uint8_t capacity,uint32_t *generation)
{
    uint32_t key;
    if (id!=1U || !samples || !generation) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();
    if (!transfer.owned || transfer.dual || !transfer.linked_count || capacity<transfer.linked_count) {
        ABoxCritical_Exit(key);return ABOX_HAL_INVALID;
    }
    if (!transfer.generation) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    for (unsigned i=0U;i<transfer.linked_count;++i) samples[i]=transfer.latest[i];
    *generation=transfer.generation;ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
void ABoxAdcDma_InjectedIrq(unsigned id)
{
    uint16_t values[4];uint8_t count;uint32_t key;const ABoxAdcLinkedEvents *events;
    if (!transfer.owned || !(count=transfer.linked_count) || transfer.fault ||
        (id!=1U && !(id==2U && transfer.dual && transfer.linked_alternate))) return;
    if (transfer.dual && transfer.linked_alternate) {
        ABoxAdcDualSample pairs[4];const ABoxAdcDualLinkedEvents *dual_events=0;uint8_t done=0U;
        if (!ABoxAdcHw_DmaInjectedFetch(id,count,values)) return;
        key=ABoxCritical_Enter();
        if (!transfer.owned || !transfer.dual || !transfer.linked_count || transfer.fault) { ABoxCritical_Exit(key);return; }
        for (unsigned i=0U;i<count;++i) {
            if (id==1U) transfer.dual_partial[i].adc1=values[i];
            else transfer.dual_partial[i].adc2=values[i];
        }
        transfer.dual_parts|=(uint8_t)(1U<<(id-1U));transfer.manual_pending=0U;
        if (transfer.dual_parts==3U) {
            for (unsigned i=0U;i<count;++i) pairs[i]=transfer.dual_latest[i]=transfer.dual_partial[i];
            transfer.dual_parts=0U;++transfer.generation;if(!transfer.generation)++transfer.generation;
            dual_events=transfer.dual_linked_events;done=1U;
        }
        ABoxCritical_Exit(key);
        if (done && dual_events && dual_events->on_samples) dual_events->on_samples(dual_events->context,pairs,count);
        return;
    }
    if (transfer.dual) {
        ABoxAdcDualSample pairs[4];const ABoxAdcDualLinkedEvents *dual_events;
        if (!ABoxAdcHw_DualInjectedFetch(count,pairs)) return;
        key=ABoxCritical_Enter();
        if (!transfer.owned || !transfer.dual || !transfer.linked_count || transfer.fault) { ABoxCritical_Exit(key);return; }
        for (unsigned i=0U;i<count;++i) transfer.dual_latest[i]=pairs[i];
        ++transfer.generation;if(!transfer.generation)++transfer.generation;
        if (transfer.observed_injected<65535U) ++transfer.observed_injected;
        transfer.manual_pending=0U;dual_events=transfer.dual_linked_events;ABoxCritical_Exit(key);
        if (dual_events && dual_events->on_samples) dual_events->on_samples(dual_events->context,pairs,count);
        return;
    }
    if (!ABoxAdcHw_DmaInjectedFetch(id,count,values)) return;
    key=ABoxCritical_Enter();
    if (!transfer.owned || !transfer.linked_count || transfer.fault) { ABoxCritical_Exit(key);return; }
    for (unsigned i=0U;i<count;++i) transfer.latest[i]=values[i];
    ++transfer.generation;if (!transfer.generation) ++transfer.generation;
    if (transfer.observed_injected<65535U) ++transfer.observed_injected;
    transfer.manual_pending=0U;events=transfer.linked_events;ABoxCritical_Exit(key);
    if (events && events->on_samples) events->on_samples(events->context,id,values,count);
}
ABoxHalStatus ABoxAdc_StartDualDma(const ABoxAdcDualDmaConfig *config,uint32_t *pairs,uint16_t count,
                                   const ABoxAdcDmaEvents *events)
{
    ABoxHalStatus status;ABoxDmaConfig dma={ABOX_DMA_PERIPHERAL_TO_MEMORY,4U,4U,0U,1U,0U,1U};
    if (!ABoxAdc_TickReady() || !config || !config->entries || !config->entry_count || config->entry_count>16U ||
        (unsigned)config->trigger>ABOX_ADC_TRIG_SOFTWARE || config->continuous>1U || config->circular>1U ||
        (unsigned)config->mode>ABOX_ADC_DUAL_INTERLEAVED_SLOW ||
        !pairs || ((uintptr_t)pairs&3U) || !count || count%config->entry_count ||
        (config->mode==ABOX_ADC_DUAL_SIMULTANEOUS && config->continuous && config->trigger!=ABOX_ADC_TRIG_SOFTWARE) ||
        (config->mode==ABOX_ADC_DUAL_INTERLEAVED_SLOW && config->continuous) ||
        (config->mode!=ABOX_ADC_DUAL_SIMULTANEOUS && config->entry_count!=1U) ||
        (config->circular && count%(2U*config->entry_count)) ||
        (config->mode!=ABOX_ADC_DUAL_INTERLEAVED_SLOW && config->trigger==ABOX_ADC_TRIG_SOFTWARE &&
         !config->continuous && count!=config->entry_count))
        return ABOX_HAL_INVALID;
    for (unsigned i=0U;i<config->entry_count;++i) {
        const ABoxAdcDualEntry *entry=&config->entries[i];
        if (!ABoxAdc_ChannelValid(1U,entry->adc1_channel,entry->sample) ||
            !ABoxAdc_ChannelValid(2U,entry->adc2_channel,entry->sample)) return ABOX_HAL_INVALID;
        if (config->mode==ABOX_ADC_DUAL_SIMULTANEOUS) {
            if (entry->adc1_channel==entry->adc2_channel) return ABOX_HAL_INVALID;
        } else if (entry->adc1_channel!=entry->adc2_channel ||
                   (config->mode==ABOX_ADC_DUAL_INTERLEAVED_FAST && entry->sample!=ABOX_ADC_SAMPLE_1_5) ||
                   (config->mode==ABOX_ADC_DUAL_INTERLEAVED_SLOW && entry->sample>ABOX_ADC_SAMPLE_13_5))
            return ABOX_HAL_INVALID;
        for (unsigned j=0U;j<i;++j)
            if ((entry->adc1_channel==config->entries[j].adc1_channel ||
                 entry->adc2_channel==config->entries[j].adc2_channel) && entry->sample!=config->entries[j].sample)
                return ABOX_HAL_INVALID;
    }
    if (config->linked) {
        const ABoxAdcDualLinkedConfig *linked=config->linked;
        if (!linked->entries || !linked->entry_count || linked->entry_count>4U ||
            (unsigned)linked->trigger>ABOX_ADC_INJ_SOFTWARE || (unsigned)linked->mode>ABOX_ADC_DUAL_INJECT_ALTERNATE || linked->automatic>1U ||
            (linked->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE && config->mode!=ABOX_ADC_DUAL_SIMULTANEOUS) ||
            (linked->automatic && (config->mode!=ABOX_ADC_DUAL_SIMULTANEOUS || linked->mode!=ABOX_ADC_DUAL_INJECT_SIMULTANEOUS ||
                                   linked->trigger!=ABOX_ADC_INJ_SOFTWARE)) ||
            (linked->events && !linked->events->on_samples)) return ABOX_HAL_INVALID;
        for (unsigned i=0U;i<linked->entry_count;++i) {
            const ABoxAdcDualEntry *e=&linked->entries[i];
            if (!ABoxAdc_ChannelValid(1U,e->adc1_channel,e->sample) || !ABoxAdc_ChannelValid(2U,e->adc2_channel,e->sample) ||
                (linked->mode==ABOX_ADC_DUAL_INJECT_SIMULTANEOUS && e->adc1_channel==e->adc2_channel)) return ABOX_HAL_INVALID;
            for (unsigned j=0U;j<i;++j)
                if ((e->adc1_channel==linked->entries[j].adc1_channel || e->adc2_channel==linked->entries[j].adc2_channel) &&
                    e->sample!=linked->entries[j].sample) return ABOX_HAL_INVALID;
            for (unsigned j=0U;j<config->entry_count;++j)
                if ((e->adc1_channel==config->entries[j].adc1_channel || e->adc2_channel==config->entries[j].adc2_channel) &&
                    e->sample!=config->entries[j].sample) return ABOX_HAL_INVALID;
        }
    }
    if (transfer.owned) return ABOX_HAL_BUSY;
    status=ABoxAdcHw_DualReserve();if(status!=ABOX_HAL_OK)return status;
    transfer.owned=1U;transfer.fault=0U;transfer.complete=0U;transfer.dual=1U;transfer.circular=config->circular;transfer.events=events;
    transfer.linked_count=config->linked ? config->linked->entry_count : 0U;
    transfer.linked_alternate=(uint8_t)(config->linked && config->linked->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE);
    transfer.dual_parts=0U;
    transfer.linked_auto=(uint8_t)(config->linked && config->linked->automatic);
    transfer.expected_injected=transfer.linked_auto ? (uint16_t)(count/config->entry_count) : 0U;
    transfer.linked_software=config->linked && !config->linked->automatic && config->linked->trigger==ABOX_ADC_INJ_SOFTWARE;
    transfer.dual_linked_events=config->linked ? config->linked->events : 0;
    transfer.generation=0U;transfer.observed_injected=0U;transfer.manual_pending=0U;
    transfer.dma_events.context=&transfer;transfer.dma_events.on_event=on_dma;dma.circular=config->circular;
    status=ABoxDma_Open(1U,1U,&dma);if(status!=ABOX_HAL_OK)goto failed;
    transfer.dma_open=1U;
    status=ABoxAdcHw_DualPrepare(config);if(status!=ABOX_HAL_OK){transfer.fault=1U;goto failed;}
    status=ABoxDma_Start(1U,1U,ABoxAdcHw_DmaAddress(1U),(uintptr_t)pairs,count,&transfer.dma_events);
    if(status!=ABOX_HAL_OK)goto failed;
    status=ABoxAdcHw_DualStart(config);if(status!=ABOX_HAL_OK){transfer.fault=1U;goto failed;}
    return ABOX_HAL_OK;
failed:
    (void)cleanup(1U);return status;
}
ABoxHalStatus ABoxAdc_StopDualDma(void)
{
    if (!transfer.owned || !transfer.dual) return ABOX_HAL_INVALID;
    if (!transfer.fault && transfer.manual_pending) return ABOX_HAL_BUSY;
    if (!transfer.fault && !transfer.circular && transfer.complete && transfer.linked_auto &&
        transfer.observed_injected<transfer.expected_injected) return ABOX_HAL_BUSY;
    return cleanup(1U);
}
ABoxHalStatus ABoxAdc_AbortDualDma(void)
{
    if (!transfer.owned || !transfer.dual) return ABOX_HAL_INVALID;
    transfer.fault=1U;return cleanup(1U);
}
ABoxHalStatus ABoxAdc_TriggerDualLinkedInjected(void)
{
    uint32_t key=ABoxCritical_Enter();ABoxHalStatus status;
    if (!transfer.owned || !transfer.dual || !transfer.linked_count || !transfer.linked_software || transfer.fault) {
        ABoxCritical_Exit(key);return ABOX_HAL_INVALID;
    }
    if (transfer.manual_pending) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    transfer.manual_pending=1U;status=ABoxAdcHw_DmaTriggerInjected(1U);
    if (status!=ABOX_HAL_OK)transfer.manual_pending=0U;
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxAdc_GetDualLinkedInjected(ABoxAdcDualSample *samples,uint8_t capacity,uint32_t *generation)
{
    uint32_t key;if(!samples || !generation)return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();
    if (!transfer.owned || !transfer.dual || !transfer.linked_count || capacity<transfer.linked_count) {
        ABoxCritical_Exit(key);return ABOX_HAL_INVALID;
    }
    if (!transfer.generation) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    for (unsigned i=0U;i<transfer.linked_count;++i) samples[i]=transfer.dual_latest[i];
    *generation=transfer.generation;ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
