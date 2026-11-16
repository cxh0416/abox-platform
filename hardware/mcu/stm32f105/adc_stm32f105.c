#include <string.h>
#include "abox_adc_common.h"
#include "abox_adc_dma_backend.h"
#include "abox_adc_watch_backend.h"
#include "abox_adc_dual_injected_backend.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_adc.h"

typedef struct { ADC_HandleTypeDef handle; uint8_t open, fault, dma_busy, injected_count; } AdcInstance;
static AdcInstance g_adc[2];
static uint8_t g_open_count, g_divider, g_dual_busy;
static ADC_TypeDef *adc(unsigned id) { return id == 1U ? ADC1 : id == 2U ? ADC2 : 0; }
static ABoxHalStatus result(HAL_StatusTypeDef s)
{
    return s == HAL_OK ? ABOX_HAL_OK : s == HAL_BUSY ? ABOX_HAL_BUSY :
           s == HAL_TIMEOUT ? ABOX_HAL_TIMEOUT : ABOX_HAL_ERROR;
}

ABoxHalStatus ABoxAdc_Open(unsigned id, const ABoxAdcConfig *config)
{
    AdcInstance *s;
    RCC_PeriphCLKInitTypeDef clock = {0};
    HAL_StatusTypeDef status;
    uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    if (!ABoxAdc_ConfigValid(id, config, pclk,0U) || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s = &g_adc[id - 1U];
    if (g_dual_busy || s->open || (g_open_count && g_divider != config->clock_divider)) return ABOX_HAL_BUSY;
    if (!g_open_count) {
        clock.PeriphClockSelection = RCC_PERIPHCLK_ADC;
        clock.AdcClockSelection = config->clock_divider == 2U ? RCC_ADCPCLK2_DIV2 :
            config->clock_divider == 4U ? RCC_ADCPCLK2_DIV4 :
            config->clock_divider == 6U ? RCC_ADCPCLK2_DIV6 : RCC_ADCPCLK2_DIV8;
        if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return ABOX_HAL_ERROR;
    }
    memset(&s->handle, 0, sizeof(s->handle));
    s->handle.Instance = adc(id);
    s->handle.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s->handle.Init.ContinuousConvMode = DISABLE;
    s->handle.Init.DiscontinuousConvMode = DISABLE;
    s->handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s->handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s->handle.Init.NbrOfConversion = 1U;
    if (id == 1U) __HAL_RCC_ADC1_CLK_ENABLE(); else __HAL_RCC_ADC2_CLK_ENABLE();
    status = HAL_ADC_Init(&s->handle);
    if (status == HAL_OK) status = HAL_ADCEx_Calibration_Start(&s->handle);
    if (status != HAL_OK) {
        (void)HAL_ADC_DeInit(&s->handle);
        if (id == 1U) __HAL_RCC_ADC1_CLK_DISABLE(); else __HAL_RCC_ADC2_CLK_DISABLE();
        return result(status);
    }
    s->open = 1U; s->fault = 0U; g_divider = config->clock_divider; ++g_open_count;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_Close(unsigned id)
{
    AdcInstance *s;
    if (!adc(id)) return ABOX_HAL_INVALID;
    s = &g_adc[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count || ABoxAdcWatch_Configured(id)) return ABOX_HAL_BUSY;
    (void)HAL_ADC_Stop(&s->handle);
    if (HAL_ADC_DeInit(&s->handle) != HAL_OK) return ABOX_HAL_ERROR;
    if (id == 1U) __HAL_RCC_ADC1_CLK_DISABLE(); else __HAL_RCC_ADC2_CLK_DISABLE();
    s->open = 0U; s->fault = 0U; --g_open_count;
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_WatchConfigure(unsigned id,const ABoxAdcWatchConfig *config)
{
    AdcInstance *s=&g_adc[id-1U];ADC_TypeDef *a=adc(id);uint32_t bits=0U;
    if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    a->CR1 &= ~(ADC_CR1_AWDCH|ADC_CR1_AWDIE|ADC_CR1_AWDSGL|ADC_CR1_JAWDEN|ADC_CR1_AWDEN);
    a->LTR=config->low;a->HTR=config->high;
    __HAL_ADC_CLEAR_FLAG(&s->handle,ADC_FLAG_AWD);
    if (config->single_channel) bits=ADC_CR1_AWDSGL|config->channel;
    if (config->regular) bits|=ADC_CR1_AWDEN;
    if (config->injected) bits|=ADC_CR1_JAWDEN;
    if (config->interrupt) bits|=ADC_CR1_AWDIE;
    a->CR1 |= bits;return ABOX_HAL_OK;
}
uint8_t ABoxAdcHw_WatchPending(unsigned id)
{ return __HAL_ADC_GET_FLAG(&g_adc[id-1U].handle,ADC_FLAG_AWD) ? 1U : 0U; }
void ABoxAdcHw_WatchClear(unsigned id) { __HAL_ADC_CLEAR_FLAG(&g_adc[id-1U].handle,ADC_FLAG_AWD); }
void ABoxAdcHw_WatchDisable(unsigned id)
{
    adc(id)->CR1 &= ~(ADC_CR1_AWDCH|ADC_CR1_AWDIE|ADC_CR1_AWDSGL|ADC_CR1_JAWDEN|ADC_CR1_AWDEN);
    ABoxAdcHw_WatchClear(id);
}
ABoxHalStatus ABoxAdc_Read(unsigned id, unsigned channel, ABoxAdcSampleTime sample,
                           uint32_t timeout_ms, uint16_t *value)
{
    AdcInstance *s;
    ADC_ChannelConfTypeDef c = {0};
    HAL_StatusTypeDef status;
    uint16_t sample_value;
    if (!ABoxAdc_ChannelValid(id, channel, sample) || !value || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s = &g_adc[id - 1U];
    if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    c.Channel = channel; c.Rank = ADC_REGULAR_RANK_1; c.SamplingTime = (uint32_t)sample;
    status = HAL_ADC_ConfigChannel(&s->handle, &c);
    if (status == HAL_OK) status = HAL_ADC_Start(&s->handle);
    if (status == HAL_OK) status = HAL_ADC_PollForConversion(&s->handle, timeout_ms);
    if (status != HAL_OK) {
        (void)HAL_ADC_Stop(&s->handle);
        if (status != HAL_BUSY) s->fault = 1U;
        return result(status);
    }
    sample_value = (uint16_t)HAL_ADC_GetValue(&s->handle);
    if (HAL_ADC_Stop(&s->handle) != HAL_OK) { s->fault = 1U; return ABOX_HAL_ERROR; }
    *value = sample_value;
    return ABOX_HAL_OK;
}

ABoxHalStatus ABoxAdc_ReadDual(unsigned channel1,unsigned channel2,ABoxAdcSampleTime sample,
                               uint32_t timeout_ms,ABoxAdcDualSample *value)
{
    ADC_HandleTypeDef *a=&g_adc[0].handle,*b=&g_adc[1].handle;
    ADC_MultiModeTypeDef mode={0};ADC_ChannelConfTypeDef entry={0};
    HAL_StatusTypeDef status,stop_a,stop_b,restore;uint32_t packed=0U;
    if (!value || channel1==channel2 || !ABoxAdc_TickReady() || !ABoxAdc_ChannelValid(1U,channel1,sample) ||
        !ABoxAdc_ChannelValid(2U,channel2,sample)) return ABOX_HAL_INVALID;
    if (!g_adc[0].open || !g_adc[1].open) return ABOX_HAL_INVALID;
    if (g_dual_busy || g_adc[0].dma_busy || g_adc[1].dma_busy ||
        g_adc[0].injected_count || g_adc[1].injected_count) return ABOX_HAL_BUSY;
    if (g_adc[0].fault || g_adc[1].fault) return ABOX_HAL_ERROR;
    g_dual_busy=1U;status=HAL_ADC_Stop(a);if(status==HAL_OK)status=HAL_ADC_Stop(b);
    mode.Mode=ADC_DUALMODE_REGSIMULT;
    if(status==HAL_OK)status=HAL_ADCEx_MultiModeConfigChannel(a,&mode);
    entry.Channel=channel1;entry.Rank=ADC_REGULAR_RANK_1;entry.SamplingTime=(uint32_t)sample;
    if(status==HAL_OK)status=HAL_ADC_ConfigChannel(a,&entry);
    entry.Channel=channel2;
    if(status==HAL_OK)status=HAL_ADC_ConfigChannel(b,&entry);
    if(status==HAL_OK && channel1>=16U) {
        uint32_t start=ABox_PortGetTickMs();while ((uint32_t)(ABox_PortGetTickMs()-start)<1U) { }
    }
    if(status==HAL_OK)status=HAL_ADC_Start(b);
    if(status==HAL_OK)status=HAL_ADC_Start(a);
    if(status==HAL_OK)status=HAL_ADC_PollForConversion(a,timeout_ms);
    if(status==HAL_OK)packed=HAL_ADCEx_MultiModeGetValue(a);
    stop_a=HAL_ADC_Stop(a);stop_b=HAL_ADC_Stop(b);mode.Mode=ADC_MODE_INDEPENDENT;
    restore=HAL_ADCEx_MultiModeConfigChannel(a,&mode);g_dual_busy=0U;
    if(status!=HAL_OK || stop_a!=HAL_OK || stop_b!=HAL_OK || restore!=HAL_OK) {
        g_adc[0].fault=1U;g_adc[1].fault=1U;
        return status!=HAL_OK ? result(status) : ABOX_HAL_ERROR;
    }
    value->adc1=(uint16_t)packed;value->adc2=(uint16_t)(packed>>16U);return ABOX_HAL_OK;
}

ABoxHalStatus ABoxAdc_StartInjected(unsigned id,const ABoxAdcInjectedConfig *config)
{
    AdcInstance *s;ADC_InjectionConfTypeDef entry={0};HAL_StatusTypeDef status;uint8_t internal=0U;
    if (!ABoxAdc_InjectedValid(id,config) || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s=&g_adc[id-1U];if (!s->open) return ABOX_HAL_INVALID;
    if (g_dual_busy || s->dma_busy || s->injected_count) return ABOX_HAL_BUSY;
    if (s->fault) return ABOX_HAL_ERROR;
    status=HAL_ADC_Stop(&s->handle);
    s->handle.Init.ScanConvMode=ADC_SCAN_ENABLE;
    if (status==HAL_OK) status=HAL_ADC_Init(&s->handle);
    entry.InjectedNbrOfConversion=config->entry_count;
    entry.ExternalTrigInjecConv=(uint32_t)config->trigger<<12U;
    entry.InjectedDiscontinuousConvMode=DISABLE;entry.AutoInjectedConv=DISABLE;entry.InjectedOffset=0U;
    for (unsigned i=0U;status==HAL_OK && i<config->entry_count;++i) {
        entry.InjectedChannel=config->entries[i].channel;entry.InjectedRank=i+1U;
        entry.InjectedSamplingTime=(uint32_t)config->entries[i].sample;
        status=HAL_ADCEx_InjectedConfigChannel(&s->handle,&entry);
        if (entry.InjectedChannel>=16U) internal=1U;
    }
    if (status==HAL_OK && internal) {
        uint32_t start=ABox_PortGetTickMs();while ((uint32_t)(ABox_PortGetTickMs()-start)<1U) { }
    }
    if (status==HAL_OK) status=HAL_ADCEx_InjectedStart(&s->handle);
    if (status!=HAL_OK) { (void)HAL_ADC_Stop(&s->handle);s->fault=1U;return result(status); }
    s->injected_count=config->entry_count;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DualInjectedStart(const ABoxAdcDualInjectedConfig *config)
{
    ADC_HandleTypeDef *a=&g_adc[0].handle,*b=&g_adc[1].handle;ADC_MultiModeTypeDef mode={0};
    ADC_InjectionConfTypeDef entry={0};HAL_StatusTypeDef status;uint8_t internal=0U;
    status=HAL_ADC_Stop(a);if(status==HAL_OK)status=HAL_ADC_Stop(b);
    for (unsigned n=0U;status==HAL_OK && n<2U;++n) {
        ADC_HandleTypeDef *h=n ? a : b;
        h->Init.ScanConvMode=ADC_SCAN_ENABLE;
        status=HAL_ADC_Init(h);
    }
    mode.Mode=config->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE ? ADC_DUALMODE_ALTERTRIG : ADC_DUALMODE_INJECSIMULT;
    if(status==HAL_OK)status=HAL_ADCEx_MultiModeConfigChannel(a,&mode);
    entry.InjectedNbrOfConversion=config->entry_count;
    entry.InjectedDiscontinuousConvMode=DISABLE;entry.AutoInjectedConv=DISABLE;entry.InjectedOffset=0U;
    for (unsigned n=0U;status==HAL_OK && n<2U;++n) {
        ADC_HandleTypeDef *h=n ? a : b;
        entry.ExternalTrigInjecConv=(uint32_t)(n ? config->trigger : ABOX_ADC_INJ_SOFTWARE)<<12U;
        for (unsigned i=0U;status==HAL_OK && i<config->entry_count;++i) {
            entry.InjectedChannel=n ? config->entries[i].adc1_channel : config->entries[i].adc2_channel;
            entry.InjectedRank=i+1U;entry.InjectedSamplingTime=(uint32_t)config->entries[i].sample;
            status=HAL_ADCEx_InjectedConfigChannel(h,&entry);
            if (n && entry.InjectedChannel>=16U) internal=1U;
        }
    }
    if(status==HAL_OK && internal) { uint32_t start=ABox_PortGetTickMs();while ((uint32_t)(ABox_PortGetTickMs()-start)<1U) { } }
    if(status==HAL_OK)status=HAL_ADCEx_InjectedStart(b);
    if(status==HAL_OK)status=HAL_ADCEx_InjectedStart(a);
    return result(status);
}
uint8_t ABoxAdcHw_DualInjectedReady(void)
{
    return (__HAL_ADC_GET_FLAG(&g_adc[0].handle,ADC_FLAG_JEOC) &&
            __HAL_ADC_GET_FLAG(&g_adc[1].handle,ADC_FLAG_JEOC)) ? 1U : 0U;
}
uint8_t ABoxAdcHw_DualInjectedFirstReady(void)
{ return __HAL_ADC_GET_FLAG(&g_adc[0].handle,ADC_FLAG_JEOC) ? 1U : 0U; }
void ABoxAdcHw_DualInjectedRead(ABoxAdcDualSample *samples,uint8_t count)
{
    adc(1U)->CR2 &= ~ADC_CR2_JEXTTRIG;
    for (unsigned i=0U;i<count;++i) {
        samples[i].adc1=(uint16_t)HAL_ADCEx_InjectedGetValue(&g_adc[0].handle,i+1U);
        samples[i].adc2=(uint16_t)HAL_ADCEx_InjectedGetValue(&g_adc[1].handle,i+1U);
    }
}
void ABoxAdcHw_DualInjectedStop(void)
{
    adc(1U)->CR2 &= ~ADC_CR2_JEXTTRIG;adc(2U)->CR2 &= ~ADC_CR2_JEXTTRIG;
    (void)HAL_ADCEx_InjectedStop(&g_adc[0].handle);(void)HAL_ADCEx_InjectedStop(&g_adc[1].handle);
}
uint8_t ABoxAdcHw_DualInjectedFetch(uint8_t count,ABoxAdcDualSample *samples)
{
    if (!(ADC1->CR1 & ADC_CR1_JEOCIE) || !ABoxAdcHw_DualInjectedReady()) return 0U;
    for (unsigned i=0U;i<count;++i) {
        samples[i].adc1=(uint16_t)HAL_ADCEx_InjectedGetValue(&g_adc[0].handle,i+1U);
        samples[i].adc2=(uint16_t)HAL_ADCEx_InjectedGetValue(&g_adc[1].handle,i+1U);
    }
    __HAL_ADC_CLEAR_FLAG(&g_adc[0].handle,ADC_FLAG_JEOC);__HAL_ADC_CLEAR_FLAG(&g_adc[1].handle,ADC_FLAG_JEOC);
    return 1U;
}
ABoxHalStatus ABoxAdc_InjectedReady(unsigned id,uint8_t *ready)
{
    if (!adc(id) || !ready || !g_adc[id-1U].open || !g_adc[id-1U].injected_count) return ABOX_HAL_INVALID;
    *ready=__HAL_ADC_GET_FLAG(&g_adc[id-1U].handle,ADC_FLAG_JEOC) ? 1U : 0U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_FinishInjected(unsigned id,uint32_t timeout_ms,uint16_t *samples,uint8_t capacity)
{
    AdcInstance *s;uint16_t result_values[4];uint32_t start;HAL_StatusTypeDef status;
    if (!adc(id) || !samples || !ABoxAdc_TickReady()) return ABOX_HAL_INVALID;
    s=&g_adc[id-1U];if (!s->open || !s->injected_count || capacity<s->injected_count) return ABOX_HAL_INVALID;
    start=ABox_PortGetTickMs();
    while (!__HAL_ADC_GET_FLAG(&s->handle,ADC_FLAG_JEOC)) {
        if ((uint32_t)(ABox_PortGetTickMs()-start)>=timeout_ms) {
            (void)HAL_ADCEx_InjectedStop(&s->handle);s->injected_count=0U;s->fault=1U;return ABOX_HAL_TIMEOUT;
        }
    }
    adc(id)->CR2 &= ~ADC_CR2_JEXTTRIG;
    for (unsigned i=0U;i<s->injected_count;++i)
        result_values[i]=(uint16_t)HAL_ADCEx_InjectedGetValue(&s->handle,i+1U);
    status=HAL_ADCEx_InjectedStop(&s->handle);
    s->handle.Init.ScanConvMode=ADC_SCAN_DISABLE;
    if (status==HAL_OK) status=HAL_ADC_Init(&s->handle);
    if (status!=HAL_OK) { s->injected_count=0U;s->fault=1U;return result(status); }
    for (unsigned i=0U;i<s->injected_count;++i) samples[i]=result_values[i];
    s->injected_count=0U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdc_AbortInjected(unsigned id)
{
    AdcInstance *s;HAL_StatusTypeDef status;
    if (!adc(id)) return ABOX_HAL_INVALID;
    s=&g_adc[id-1U];if (!s->open || !s->injected_count) return ABOX_HAL_INVALID;
    status=HAL_ADCEx_InjectedStop(&s->handle);s->injected_count=0U;s->fault=1U;return result(status);
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
    ADC_HandleTypeDef *h=&g_adc[id-1U].handle; ADC_ChannelConfTypeDef entry={0};
    ADC_InjectionConfTypeDef injected={0};
    uint8_t internal=0U;
    if (HAL_ADC_Stop(h)!=HAL_OK) return ABOX_HAL_ERROR;
    h->Instance->CR1 &= ~(ADC_CR1_JAUTO|ADC_CR1_JDISCEN|ADC_CR1_JEOCIE);
    h->Instance->CR2 &= ~ADC_CR2_JEXTTRIG;
    h->Init.ScanConvMode=(config->entry_count>1U || config->linked) ? ADC_SCAN_ENABLE : ADC_SCAN_DISABLE;
    h->Init.ContinuousConvMode=config->continuous ? ENABLE : DISABLE;
    h->Init.ExternalTrigConv=(uint32_t)config->trigger<<17U;
    h->Init.NbrOfConversion=config->entry_count;
    if (HAL_ADC_Init(h)!=HAL_OK) return ABOX_HAL_ERROR;
    for (unsigned i=0U;i<config->entry_count;++i) {
        entry.Channel=config->entries[i].channel;entry.Rank=i+1U;entry.SamplingTime=(uint32_t)config->entries[i].sample;
        if (HAL_ADC_ConfigChannel(h,&entry)!=HAL_OK) return ABOX_HAL_ERROR;
        if (entry.Channel>=16U) internal=1U;
    }
    if (config->linked) {
        const ABoxAdcLinkedConfig *linked=config->linked;
        injected.InjectedNbrOfConversion=linked->entry_count;
        injected.ExternalTrigInjecConv=(uint32_t)linked->trigger<<12U;
        injected.InjectedDiscontinuousConvMode=DISABLE;
        injected.AutoInjectedConv=linked->mode==ABOX_ADC_INJECT_AFTER_REGULAR ? ENABLE : DISABLE;
        injected.InjectedOffset=0U;
        for (unsigned i=0U;i<linked->entry_count;++i) {
            injected.InjectedChannel=linked->entries[i].channel;injected.InjectedRank=i+1U;
            injected.InjectedSamplingTime=(uint32_t)linked->entries[i].sample;
            if (HAL_ADCEx_InjectedConfigChannel(h,&injected)!=HAL_OK) return ABOX_HAL_ERROR;
            if (injected.InjectedChannel>=16U) internal=1U;
        }
    }
    /* Vref may have enabled the shared sensor before its later sequence slot;
     * allow both internal channels to settle independent of entry ordering. */
    if (internal) { uint32_t start=ABox_PortGetTickMs(); while ((uint32_t)(ABox_PortGetTickMs()-start)<1U) { } }
    return ABOX_HAL_OK;
}
uintptr_t ABoxAdcHw_DmaAddress(unsigned id) { return (uintptr_t)&adc(id)->DR; }
ABoxHalStatus ABoxAdcHw_DmaStart(unsigned id,const ABoxAdcDmaConfig *config)
{
    ADC_HandleTypeDef *h=&g_adc[id-1U].handle;
    if (config->linked) {
        __HAL_ADC_CLEAR_FLAG(h,ADC_FLAG_JEOC);h->Instance->CR1 |= ADC_CR1_JEOCIE;
        if (config->linked->mode==ABOX_ADC_INJECT_TRIGGERED && config->linked->trigger!=ABOX_ADC_INJ_SOFTWARE)
            h->Instance->CR2 |= ADC_CR2_JEXTTRIG;
    }
    h->Instance->CR2 |= ADC_CR2_DMA;return result(HAL_ADC_Start(h));
}
ABoxHalStatus ABoxAdcHw_DmaTriggerInjected(unsigned id)
{ adc(id)->CR2 |= ADC_CR2_JEXTTRIG|ADC_CR2_JSWSTART;return ABOX_HAL_OK; }
uint8_t ABoxAdcHw_DmaInjectedFetch(unsigned id,uint8_t count,uint16_t *values)
{
    ADC_HandleTypeDef *h=&g_adc[id-1U].handle;
    if (!(h->Instance->CR1 & ADC_CR1_JEOCIE) || !__HAL_ADC_GET_FLAG(h,ADC_FLAG_JEOC)) return 0U;
    for (unsigned i=0U;i<count;++i) values[i]=(uint16_t)HAL_ADCEx_InjectedGetValue(h,i+1U);
    __HAL_ADC_CLEAR_FLAG(h,ADC_FLAG_JEOC);return 1U;
}
void ABoxAdcHw_DmaInjectedMask(unsigned id)
{ adc(id)->CR1 &= ~ADC_CR1_JEOCIE;adc(id)->CR2 &= ~ADC_CR2_JEXTTRIG; }
void ABoxAdcHw_DmaPause(unsigned id) { adc(id)->CR2 &= ~(ADC_CR2_DMA|ADC_CR2_CONT|ADC_CR2_EXTTRIG); }
ABoxHalStatus ABoxAdcHw_DmaRelease(unsigned id,int fault)
{
    AdcInstance *s=&g_adc[id-1U];ADC_HandleTypeDef *h=&s->handle; HAL_StatusTypeDef status;
    ABoxAdcHw_DmaPause(id);status=HAL_ADC_Stop(h);
    h->Instance->CR1 &= ~(ADC_CR1_JAUTO|ADC_CR1_JEOCIE);
    h->Instance->CR2 &= ~ADC_CR2_JEXTTRIG;__HAL_ADC_CLEAR_FLAG(h,ADC_FLAG_JEOC);
    h->Init.ScanConvMode=ADC_SCAN_DISABLE;h->Init.ContinuousConvMode=DISABLE;
    h->Init.ExternalTrigConv=ADC_SOFTWARE_START;h->Init.NbrOfConversion=1U;
    if (status==HAL_OK) status=HAL_ADC_Init(h);
    s->dma_busy=0U;if (fault || status!=HAL_OK) s->fault=1U;
    return result(status);
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
    ADC_HandleTypeDef *a=&g_adc[0].handle,*b=&g_adc[1].handle;
    ADC_MultiModeTypeDef mode={0};ADC_ChannelConfTypeDef entry={0};uint8_t internal=0U;
    if (HAL_ADC_Stop(a)!=HAL_OK || HAL_ADC_Stop(b)!=HAL_OK) return ABOX_HAL_ERROR;
    for (unsigned i=0U;i<2U;++i) {
        ADC_HandleTypeDef *h=i ? b : a;
        h->Init.ScanConvMode=(config->entry_count>1U || config->linked) ? ADC_SCAN_ENABLE : ADC_SCAN_DISABLE;
        h->Init.ContinuousConvMode=config->continuous ? ENABLE : DISABLE;
        h->Init.ExternalTrigConv=i ? ADC_SOFTWARE_START : (uint32_t)config->trigger<<17U;
        h->Init.NbrOfConversion=config->entry_count;
        if (HAL_ADC_Init(h)!=HAL_OK) return ABOX_HAL_ERROR;
    }
    mode.Mode=config->linked ? (config->mode==ABOX_ADC_DUAL_INTERLEAVED_FAST ? ADC_DUALMODE_INJECSIMULT_INTERLFAST :
              config->mode==ABOX_ADC_DUAL_INTERLEAVED_SLOW ? ADC_DUALMODE_INJECSIMULT_INTERLSLOW :
              config->linked->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE ? ADC_DUALMODE_REGSIMULT_ALTERTRIG : ADC_DUALMODE_REGSIMULT_INJECSIMULT) :
              (config->mode==ABOX_ADC_DUAL_INTERLEAVED_FAST ? ADC_DUALMODE_INTERLFAST :
              config->mode==ABOX_ADC_DUAL_INTERLEAVED_SLOW ? ADC_DUALMODE_INTERLSLOW : ADC_DUALMODE_REGSIMULT);
    if (HAL_ADCEx_MultiModeConfigChannel(a,&mode)!=HAL_OK) return ABOX_HAL_ERROR;
    for (unsigned i=0U;i<config->entry_count;++i) {
        entry.Rank=i+1U;entry.SamplingTime=(uint32_t)config->entries[i].sample;
        entry.Channel=config->entries[i].adc1_channel;
        if (HAL_ADC_ConfigChannel(a,&entry)!=HAL_OK) return ABOX_HAL_ERROR;
        if (entry.Channel>=16U) internal=1U;
        entry.Channel=config->entries[i].adc2_channel;
        if (HAL_ADC_ConfigChannel(b,&entry)!=HAL_OK) return ABOX_HAL_ERROR;
    }
    if (config->linked) {
        ADC_InjectionConfTypeDef inj={0};const ABoxAdcDualLinkedConfig *linked=config->linked;
        inj.InjectedNbrOfConversion=linked->entry_count;inj.InjectedDiscontinuousConvMode=DISABLE;
        inj.AutoInjectedConv=linked->automatic ? ENABLE : DISABLE;inj.InjectedOffset=0U;
        for (unsigned n=0U;n<2U;++n) {
            ADC_HandleTypeDef *h=n ? a : b;
            inj.ExternalTrigInjecConv=(uint32_t)(n ? linked->trigger : ABOX_ADC_INJ_SOFTWARE)<<12U;
            for (unsigned i=0U;i<linked->entry_count;++i) {
                inj.InjectedChannel=n ? linked->entries[i].adc1_channel : linked->entries[i].adc2_channel;
                inj.InjectedRank=i+1U;inj.InjectedSamplingTime=(uint32_t)linked->entries[i].sample;
                if (HAL_ADCEx_InjectedConfigChannel(h,&inj)!=HAL_OK) return ABOX_HAL_ERROR;
                if (n && inj.InjectedChannel>=16U) internal=1U;
            }
        }
    }
    if (internal) { uint32_t start=ABox_PortGetTickMs();while ((uint32_t)(ABox_PortGetTickMs()-start)<1U) { } }
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxAdcHw_DualStart(const ABoxAdcDualDmaConfig *config)
{
    HAL_StatusTypeDef status;
    if (config->linked) {
        __HAL_ADC_CLEAR_FLAG(&g_adc[0].handle,ADC_FLAG_JEOC);__HAL_ADC_CLEAR_FLAG(&g_adc[1].handle,ADC_FLAG_JEOC);
        ADC1->CR1 |= ADC_CR1_JEOCIE;
        if (config->linked->mode==ABOX_ADC_DUAL_INJECT_ALTERNATE) ADC2->CR1 |= ADC_CR1_JEOCIE;
        if (!config->linked->automatic) {
            ADC2->CR2 |= ADC_CR2_JEXTTRIG;
            if (config->linked->trigger!=ABOX_ADC_INJ_SOFTWARE) ADC1->CR2 |= ADC_CR2_JEXTTRIG;
        }
    }
    status=HAL_ADC_Start(&g_adc[1].handle);
    if (status==HAL_OK) { ADC1->CR2 |= ADC_CR2_DMA;status=HAL_ADC_Start(&g_adc[0].handle); }
    return result(status);
}
void ABoxAdcHw_DualRegularPause(void)
{
    ADC1->CR2 &= ~(ADC_CR2_DMA|ADC_CR2_CONT|ADC_CR2_EXTTRIG);
    ADC2->CR2 &= ~(ADC_CR2_CONT|ADC_CR2_EXTTRIG);
}
void ABoxAdcHw_DualPause(void)
{
    /* Slow interleaving keeps converting after its first trigger even with
     * CONT clear. Stop both ADCs as well as masking triggers and DMA. */
    ADC1->CR2 &= ~(ADC_CR2_DMA|ADC_CR2_CONT|ADC_CR2_EXTTRIG|ADC_CR2_ADON);
    ADC2->CR2 &= ~(ADC_CR2_CONT|ADC_CR2_EXTTRIG|ADC_CR2_ADON);
}
ABoxHalStatus ABoxAdcHw_DualRelease(int fault)
{
    ADC_HandleTypeDef *a=&g_adc[0].handle,*b=&g_adc[1].handle;
    ADC_MultiModeTypeDef mode={0};HAL_StatusTypeDef sa,sb,sm;
    ABoxAdcHw_DualPause();sa=HAL_ADC_Stop(a);sb=HAL_ADC_Stop(b);mode.Mode=ADC_MODE_INDEPENDENT;
    sm=HAL_ADCEx_MultiModeConfigChannel(a,&mode);
    for (unsigned i=0U;i<2U;++i) {
        ADC_HandleTypeDef *h=i ? b : a;HAL_StatusTypeDef status;
        h->Instance->CR1 &= ~(ADC_CR1_JAUTO|ADC_CR1_JEOCIE);
        h->Instance->CR2 &= ~ADC_CR2_JEXTTRIG;__HAL_ADC_CLEAR_FLAG(h,ADC_FLAG_JEOC);
        h->Init.ScanConvMode=ADC_SCAN_DISABLE;h->Init.ContinuousConvMode=DISABLE;
        h->Init.ExternalTrigConv=ADC_SOFTWARE_START;h->Init.NbrOfConversion=1U;
        status=HAL_ADC_Init(h);if (status!=HAL_OK) sm=status;
    }
    g_dual_busy=0U;
    if (fault || sa!=HAL_OK || sb!=HAL_OK || sm!=HAL_OK) { g_adc[0].fault=1U;g_adc[1].fault=1U; }
    return (sa==HAL_OK && sb==HAL_OK && sm==HAL_OK) ? ABOX_HAL_OK : ABOX_HAL_ERROR;
}
