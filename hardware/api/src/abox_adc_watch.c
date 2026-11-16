#include "abox_adc_watch_backend.h"
#include "abox_critical.h"
#include "abox_adc_dma_backend.h"
typedef struct { const ABoxAdcWatchEvents *events;uint8_t configured,interrupt; } Watch;
static Watch watches[2];
uint8_t ABoxAdcWatch_Configured(unsigned id)
{ return id>=1U && id<=2U ? watches[id-1U].configured : 0U; }
ABoxHalStatus ABoxAdc_WatchConfigure(unsigned id,const ABoxAdcWatchConfig *config,const ABoxAdcWatchEvents *events)
{
    uint32_t key;ABoxHalStatus status;Watch *watch;
    if (id<1U || id>2U || !config || config->low>config->high || config->high>4095U ||
        config->regular>1U || config->injected>1U || (!config->regular && !config->injected) ||
        config->single_channel>1U || config->interrupt>1U ||
        (config->single_channel && (config->channel>17U || (id==2U && config->channel>15U)))) return ABOX_HAL_INVALID;
    key=ABoxCritical_Enter();watch=&watches[id-1U];
    if (watch->configured) { ABoxCritical_Exit(key);return ABOX_HAL_BUSY; }
    status=ABoxAdcHw_WatchConfigure(id,config);
    if (status==ABOX_HAL_OK) { watch->events=events;watch->interrupt=config->interrupt;watch->configured=1U; }
    ABoxCritical_Exit(key);return status;
}
ABoxHalStatus ABoxAdc_WatchRead(unsigned id,uint8_t *pending)
{
    if (!pending || !ABoxAdcWatch_Configured(id)) return ABOX_HAL_INVALID;
    *pending=ABoxAdcHw_WatchPending(id);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_WatchClear(unsigned id)
{
    uint32_t key=ABoxCritical_Enter();
    if (!ABoxAdcWatch_Configured(id)) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    ABoxAdcHw_WatchClear(id);ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_WatchDisable(unsigned id)
{
    uint32_t key=ABoxCritical_Enter();Watch *watch;
    if (!ABoxAdcWatch_Configured(id)) { ABoxCritical_Exit(key);return ABOX_HAL_INVALID; }
    watch=&watches[id-1U];ABoxAdcHw_WatchDisable(id);
    watch->configured=0U;watch->interrupt=0U;watch->events=0;
    ABoxCritical_Exit(key);return ABOX_HAL_OK;
}
void ABoxAdc_Irq(unsigned id)
{
    Watch *watch;const ABoxAdcWatchEvents *events;
    if (ABoxAdcWatch_Configured(id)) {
        watch=&watches[id-1U];
        if (watch->interrupt && ABoxAdcHw_WatchPending(id)) {
            ABoxAdcHw_WatchClear(id);events=watch->events;
            if (events && events->out_of_range) events->out_of_range(events->context,id);
        }
    }
    ABoxAdcDma_InjectedIrq(id);
}
