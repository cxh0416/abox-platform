#ifndef TEST_ADC_GD_H
#define TEST_ADC_GD_H
#include <stdint.h>
typedef enum { RESET=0, SET=1 } FlagStatus;
typedef enum { DISABLE=0, ENABLE=1 } ControlStatus;
#define ADC0 1U
#define ADC1 2U
#define RCU_ADC0 1U
#define RCU_ADC1 2U
#define CK_APB2 2U
#define RCU_CKADC_CKAPB2_DIV2 2U
#define RCU_CKADC_CKAPB2_DIV4 4U
#define RCU_CKADC_CKAPB2_DIV6 6U
#define RCU_CKADC_CKAPB2_DIV8 8U
#define RCU_CKADC_CKAPB2_DIV12 12U
#define RCU_CKADC_CKAPB2_DIV16 16U
#define ADC_MODE_FREE 0U
#define ADC_DAUL_REGULAL_PARALLEL (6U<<16U)
#define ADC_DAUL_REGULAL_FOLLOWUP_FAST (7U<<16U)
#define ADC_DAUL_REGULAL_FOLLOWUP_SLOW (8U<<16U)
#define ADC_DATAALIGN_RIGHT 0U
#define ADC_SCAN_MODE 1U
#define ADC_CONTINUOUS_MODE 2U
#define ADC_INSERTED_CHANNEL_AUTO (1U<<10)
#define ADC_CTL0_EOICIE (1U<<7)
#define ADC_REGULAR_CHANNEL 1U
#define ADC_INSERTED_CHANNEL 2U
#define ADC0_1_2_EXTTRIG_REGULAR_NONE 0U
#define ADC_CTL1_RSTCLB 1U
#define ADC_CTL1_CLB 2U
#define ADC_FLAG_WDE 1U
#define ADC_FLAG_EOC 2U
#define ADC_FLAG_EOIC 4U
extern uint32_t fake_adc_ctl[2], fake_adc_data[2], fake_adc_ctl0[2], fake_adc_low[2], fake_adc_high[2];
#define ADC_RDATA(instance) (fake_adc_data[(instance)-1U])
#define ADC_CTL1(instance) (fake_adc_ctl[(instance)-1U])
#define ADC_CTL0(instance) (fake_adc_ctl0[(instance)-1U])
#define ADC_WDLT(instance) (fake_adc_low[(instance)-1U])
#define ADC_WDHT(instance) (fake_adc_high[(instance)-1U])
#define ADC_CTL0_WDCHSEL 0x1fU
#define ADC_CTL0_WDEIE (1U<<6)
#define ADC_CTL0_WDSC (1U<<9)
#define ADC_CTL0_IWDEN (1U<<22)
#define ADC_CTL0_RWDEN (1U<<23)
void rcu_periph_clock_enable(uint32_t id);
void rcu_periph_clock_disable(uint32_t id);
uint32_t rcu_clock_freq_get(uint32_t clock);
void rcu_adc_clock_config(uint32_t divider);
void adc_mode_config(uint32_t mode);
void adc_deinit(uint32_t id);
void adc_data_alignment_config(uint32_t id,uint32_t align);
void adc_special_function_config(uint32_t id,uint32_t f,ControlStatus state);
void adc_channel_length_config(uint32_t id,uint8_t group,uint32_t n);
void adc_external_trigger_source_config(uint32_t id,uint8_t group,uint32_t source);
void adc_external_trigger_config(uint32_t id,uint8_t group,ControlStatus state);
void adc_dma_mode_enable(uint32_t id);
void adc_dma_mode_disable(uint32_t id);
void adc_enable(uint32_t id);
void adc_disable(uint32_t id);
void adc_tempsensor_vrefint_enable(void);
void adc_tempsensor_vrefint_disable(void);
void adc_regular_channel_config(uint32_t id,uint8_t rank,uint8_t ch,uint32_t sample);
void adc_inserted_channel_config(uint32_t id,uint8_t rank,uint8_t ch,uint32_t sample);
void adc_inserted_channel_offset_config(uint32_t id,uint8_t rank,uint16_t offset);
uint16_t adc_inserted_data_read(uint32_t id,uint8_t rank);
void adc_flag_clear(uint32_t id,uint32_t f);
void adc_software_trigger_enable(uint32_t id,uint8_t group);
FlagStatus adc_flag_get(uint32_t id,uint32_t f);
uint16_t adc_regular_data_read(uint32_t id);
uint32_t adc_sync_mode_convert_value_read(void);
#endif
