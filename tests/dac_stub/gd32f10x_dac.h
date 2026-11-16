#ifndef GD32F10X_DAC_H
#define GD32F10X_DAC_H
#include <stdint.h>
#define DAC0 0U
#define DAC_OUT0 0U
extern uint32_t fake_dac_data[2];
#define DAC_OUT0_R12DH(d) fake_dac_data[0]
#define DAC_OUT1_R12DH(d) fake_dac_data[1]
#define DAC_OUT1 1U
#define DAC_ALIGN_12B_R 0U
#define DAC_WAVE_DISABLE 0U
#define DAC_WAVE_MODE_LFSR (1U<<6U)
#define DAC_WAVE_MODE_TRIANGLE (2U<<6U)
#define DAC_TRIGGER_SOFTWARE 7U
#define DAC_TRIGGER_T1_TRGO 4U
#define DAC_TRIGGER_T2_TRGO 1U
#define DAC_TRIGGER_T3_TRGO 5U
#define DAC_TRIGGER_T4_TRGO 3U
#define DAC_TRIGGER_T5_TRGO 10U
#define DAC_TRIGGER_T6_TRGO 2U
#define DAC_TRIGGER_EXTI_9 6U
void dac_enable(uint32_t,uint8_t);void dac_disable(uint32_t,uint8_t);void dac_dma_disable(uint32_t,uint8_t);
void dac_dma_enable(uint32_t,uint8_t);
void dac_trigger_disable(uint32_t,uint8_t);void dac_trigger_enable(uint32_t,uint8_t);
void dac_wave_mode_config(uint32_t,uint8_t,uint32_t);
void dac_lfsr_noise_config(uint32_t,uint8_t,uint32_t);
void dac_triangle_noise_config(uint32_t,uint8_t,uint32_t);
void dac_output_buffer_enable(uint32_t,uint8_t);void dac_output_buffer_disable(uint32_t,uint8_t);
void dac_trigger_source_config(uint32_t,uint8_t,uint32_t);
void dac_data_set(uint32_t,uint8_t,uint32_t,uint16_t);
void dac_software_trigger_enable(uint32_t,uint8_t);
uint16_t dac_output_value_get(uint32_t,uint8_t);
#endif
