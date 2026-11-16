#ifndef ABOX_ADC_COMMON_H
#define ABOX_ADC_COMMON_H
#include "abox_adc.h"
#include "abox_platform_port.h"
static inline int ABoxAdc_ConfigValid(unsigned id, const ABoxAdcConfig *c, uint32_t pclk,uint8_t extended_dividers)
{
    return id >= 1U && id <= 2U && c &&
           (c->clock_divider == 2U || c->clock_divider == 4U ||
            c->clock_divider == 6U || c->clock_divider == 8U ||
            (extended_dividers && (c->clock_divider == 12U || c->clock_divider == 16U))) &&
           pclk && (uint64_t)pclk <= 14000000ULL * c->clock_divider;
}
static inline int ABoxAdc_ChannelValid(unsigned id, unsigned channel, ABoxAdcSampleTime sample)
{
    return id >= 1U && id <= 2U && channel <= 17U && (unsigned)sample <= ABOX_ADC_SAMPLE_239_5 &&
           (channel < 16U || (id == 1U && sample == ABOX_ADC_SAMPLE_239_5));
}
static inline int ABoxAdc_TickReady(void)
{
    const ABoxPlatformPort *p = ABox_PlatformPortGet();
    return p && p->get_tick_ms;
}
static inline int ABoxAdc_InjectedValid(unsigned id,const ABoxAdcInjectedConfig *config)
{
    if (id<1U || id>2U || !config || !config->entries || !config->entry_count || config->entry_count>4U ||
        (unsigned)config->trigger>ABOX_ADC_INJ_SOFTWARE) return 0;
    for (unsigned i=0U;i<config->entry_count;++i) {
        if (!ABoxAdc_ChannelValid(id,config->entries[i].channel,config->entries[i].sample)) return 0;
        for (unsigned j=0U;j<i;++j)
            if (config->entries[i].channel==config->entries[j].channel &&
                config->entries[i].sample!=config->entries[j].sample) return 0;
    }
    return 1;
}
#endif
