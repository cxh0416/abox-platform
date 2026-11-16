#include "abox_dac_backend.h"
#include "gd32f10x.h"
#include "gd32f10x_dac.h"
static uint8_t channel_id(unsigned channel) { return channel==1U ? DAC_OUT0 : DAC_OUT1; }
ABoxHalStatus ABoxDacHw_Open(unsigned channel, const ABoxDacConfig *config)
{
    static const uint32_t triggers[]={0U,DAC_TRIGGER_SOFTWARE,DAC_TRIGGER_T1_TRGO,
        DAC_TRIGGER_T2_TRGO,DAC_TRIGGER_T3_TRGO,DAC_TRIGGER_T4_TRGO,DAC_TRIGGER_T5_TRGO,
        DAC_TRIGGER_T6_TRGO,DAC_TRIGGER_EXTI_9};
    uint8_t out=channel_id(channel);
    rcu_periph_clock_enable(RCU_DAC);
    dac_disable(DAC0,out);
    dac_dma_disable(DAC0,out);
    dac_trigger_disable(DAC0,out);
    dac_wave_mode_config(DAC0,out,DAC_WAVE_DISABLE);
    if (config->output_buffer) dac_output_buffer_enable(DAC0,out); else dac_output_buffer_disable(DAC0,out);
    if (config->trigger != ABOX_DAC_IMMEDIATE) {
        dac_trigger_source_config(DAC0,out,triggers[config->trigger]); dac_trigger_enable(DAC0,out);
    }
    if (config->wave==ABOX_DAC_WAVE_TRIANGLE) {
        dac_triangle_noise_config(DAC0,out,(uint32_t)(config->wave_bits-1U)<<8U);
        dac_wave_mode_config(DAC0,out,DAC_WAVE_MODE_TRIANGLE);
    } else if (config->wave==ABOX_DAC_WAVE_NOISE) {
        dac_lfsr_noise_config(DAC0,out,(uint32_t)(config->wave_bits-1U)<<8U);
        dac_wave_mode_config(DAC0,out,DAC_WAVE_MODE_LFSR);
    }
    dac_data_set(DAC0,out,DAC_ALIGN_12B_R,config->initial_value);
    return ABOX_HAL_OK;
}
void ABoxDacHw_Enable(unsigned channel) { dac_enable(DAC0,channel_id(channel)); }
void ABoxDacHw_Disable(unsigned channel) { dac_disable(DAC0,channel_id(channel)); }
void ABoxDacHw_Write(unsigned channel,uint16_t value) { dac_data_set(DAC0,channel_id(channel),DAC_ALIGN_12B_R,value); }
void ABoxDacHw_Trigger(unsigned channel) { dac_software_trigger_enable(DAC0,channel_id(channel)); }
uint16_t ABoxDacHw_ReadOutput(unsigned channel) { return dac_output_value_get(DAC0,channel_id(channel)); }

uintptr_t ABoxDacHw_DmaAddress(unsigned channel)
{ return channel==1U ? (uintptr_t)&DAC_OUT0_R12DH(DAC0) : (uintptr_t)&DAC_OUT1_R12DH(DAC0); }
void ABoxDacHw_DmaRequest(unsigned channel,int enable)
{ if (enable) dac_dma_enable(DAC0,channel_id(channel)); else dac_dma_disable(DAC0,channel_id(channel)); }
