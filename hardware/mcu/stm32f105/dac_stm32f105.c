#include "abox_dac_backend.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_dac.h"
static DAC_HandleTypeDef handle;
static uint8_t initialized;
static uint32_t channel_id(unsigned channel) { return channel==1U ? DAC_CHANNEL_1 : DAC_CHANNEL_2; }
ABoxHalStatus ABoxDacHw_Open(unsigned channel, const ABoxDacConfig *config)
{
    static const uint32_t triggers[]={DAC_TRIGGER_NONE,DAC_TRIGGER_SOFTWARE,DAC_TRIGGER_T2_TRGO,
        DAC_TRIGGER_T3_TRGO,DAC_TRIGGER_T4_TRGO,DAC_TRIGGER_T5_TRGO,DAC_TRIGGER_T6_TRGO,
        DAC_TRIGGER_T7_TRGO,DAC_TRIGGER_EXT_IT9};
    DAC_ChannelConfTypeDef setup={0};
    __HAL_RCC_DAC_CLK_ENABLE();
    if (!initialized) {
        handle.Instance=DAC;
        if (HAL_DAC_Init(&handle)!=HAL_OK) return ABOX_HAL_ERROR;
        initialized=1U;
    }
    __HAL_DAC_DISABLE(&handle,channel_id(channel));
    DAC->CR &= ~(DAC_CR_DMAEN1 << channel_id(channel));
    setup.DAC_Trigger=triggers[config->trigger];
    setup.DAC_OutputBuffer=config->output_buffer ? DAC_OUTPUTBUFFER_ENABLE : DAC_OUTPUTBUFFER_DISABLE;
    if (HAL_DAC_ConfigChannel(&handle,&setup,channel_id(channel))!=HAL_OK ||
        HAL_DAC_SetValue(&handle,channel_id(channel),DAC_ALIGN_12B_R,config->initial_value)!=HAL_OK) return ABOX_HAL_ERROR;
    if (config->wave==ABOX_DAC_WAVE_TRIANGLE &&
        HAL_DACEx_TriangleWaveGenerate(&handle,channel_id(channel),(uint32_t)(config->wave_bits-1U)<<8U)!=HAL_OK) return ABOX_HAL_ERROR;
    if (config->wave==ABOX_DAC_WAVE_NOISE &&
        HAL_DACEx_NoiseWaveGenerate(&handle,channel_id(channel),(uint32_t)(config->wave_bits-1U)<<8U)!=HAL_OK) return ABOX_HAL_ERROR;
    return ABOX_HAL_OK;
}
void ABoxDacHw_Enable(unsigned channel) { __HAL_DAC_ENABLE(&handle,channel_id(channel)); }
void ABoxDacHw_Disable(unsigned channel) { __HAL_DAC_DISABLE(&handle,channel_id(channel)); }
void ABoxDacHw_Write(unsigned channel,uint16_t value) { (void)HAL_DAC_SetValue(&handle,channel_id(channel),DAC_ALIGN_12B_R,value); }
void ABoxDacHw_Trigger(unsigned channel) { DAC->SWTRIGR = channel==1U ? DAC_SWTRIGR_SWTRIG1 : DAC_SWTRIGR_SWTRIG2; }
uint16_t ABoxDacHw_ReadOutput(unsigned channel) { return (uint16_t)HAL_DAC_GetValue(&handle,channel_id(channel)); }

uintptr_t ABoxDacHw_DmaAddress(unsigned channel)
{ return channel==1U ? (uintptr_t)&DAC->DHR12R1 : (uintptr_t)&DAC->DHR12R2; }
void ABoxDacHw_DmaRequest(unsigned channel,int enable)
{
    uint32_t bit=DAC_CR_DMAEN1 << channel_id(channel);
    if (enable) DAC->CR |= bit; else DAC->CR &= ~bit;
}
