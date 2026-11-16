#include "abox_adc_common.h"
#include "abox_adc_dma_backend.h"
#include "abox_adc_dual_injected_backend.h"
#include "abox_platform_port.h"

static uint8_t injected_count;
static uint8_t alternate_software, second_started;
ABoxHalStatus ABoxAdc_StartDualInjected(const ABoxAdcDualInjectedConfig *config)
{
    ABoxHalStatus status;
    if (!config || !config->entries || !config->entry_count || config->entry_count>4U ||
        (unsigned)config->trigger>ABOX_ADC_INJ_SOFTWARE || (unsigned)config->mode>ABOX_ADC_DUAL_INJECT_ALTERNATE ||
        !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    for (unsigned i=0U;i<config->entry_count;++i) {
        const ABoxAdcDualEntry *e=&config->entries[i];
        if (!ABoxAdc_ChannelValid(1U,e->adc1_channel,e->sample) ||
            !ABoxAdc_ChannelValid(2U,e->adc2_channel,e->sample) ||
            (config->mode==ABOX_ADC_DUAL_INJECT_SIMULTANEOUS && e->adc1_channel==e->adc2_channel)) return ABOX_HAL_INVALID;
        for (unsigned j=0U;j<i;++j)
            if ((e->adc1_channel==config->entries[j].adc1_channel || e->adc2_channel==config->entries[j].adc2_channel) &&
                e->sample!=config->entries[j].sample) return ABOX_HAL_INVALID;
    }
    if (injected_count) return ABOX_HAL_BUSY;
    status=ABoxAdcHw_DualReserve();if(status!=ABOX_HAL_OK)return status;
    status=ABoxAdcHw_DualInjectedStart(config);
    if (status!=ABOX_HAL_OK) { ABoxAdcHw_DualInjectedStop();(void)ABoxAdcHw_DualRelease(1);return status; }
    injected_count=config->entry_count;second_started=0U;
    alternate_software=(uint8_t)(config->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE && config->trigger==ABOX_ADC_INJ_SOFTWARE);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_AdvanceDualInjected(void)
{
    ABoxHalStatus status;
    if (!injected_count || !alternate_software) return ABOX_HAL_INVALID;
    if (second_started || !ABoxAdcHw_DualInjectedFirstReady()) return ABOX_HAL_BUSY;
    status=ABoxAdcHw_DmaTriggerInjected(1U);
    if (status==ABOX_HAL_OK) second_started=1U;
    return status;
}
ABoxHalStatus ABoxAdc_DualInjectedReady(uint8_t *ready)
{
    if (!ready || !injected_count) return ABOX_HAL_INVALID;
    *ready=(alternate_software && !second_started) ? 0U : ABoxAdcHw_DualInjectedReady();return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_FinishDualInjected(uint32_t timeout_ms,ABoxAdcDualSample *samples,uint8_t capacity)
{
    ABoxAdcDualSample values[4];ABoxHalStatus status;uint32_t start;
    if (!samples || !injected_count || capacity<injected_count || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    start=ABox_PortGetTickMs();
    while ((alternate_software && !second_started) || !ABoxAdcHw_DualInjectedReady()) {
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout_ms) {
            ABoxAdcHw_DualInjectedStop();(void)ABoxAdcHw_DualRelease(1);injected_count=0U;return ABOX_HAL_TIMEOUT;
        }
    }
    ABoxAdcHw_DualInjectedRead(values,injected_count);
    ABoxAdcHw_DualInjectedStop();status=ABoxAdcHw_DualRelease(0);
    if (status==ABOX_HAL_OK) for (unsigned i=0U;i<injected_count;++i) samples[i]=values[i];
    injected_count=0U;return status;
}
ABoxHalStatus ABoxAdc_AbortDualInjected(void)
{
    ABoxHalStatus status;
    if (!injected_count) return ABOX_HAL_INVALID;
    ABoxAdcHw_DualInjectedStop();status=ABoxAdcHw_DualRelease(1);injected_count=0U;return status;
}
