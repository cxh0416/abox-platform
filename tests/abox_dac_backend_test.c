#include <assert.h>
#include "abox_dac_backend.h"
static uint32_t chosen_trigger, chosen_channel, written_value;
static unsigned buffer_on, trigger_on, trigger_off, triggers, dma_enabled, wave_mode, wave_bits;
uint32_t fake_dac_data[2];
unsigned clocks,enabled,disabled;
#ifdef TEST_STM32
#include "stm32f1xx_hal_dac.h"
DAC_TypeDef dac_registers;
static unsigned initializations; static int fail_config;
int HAL_DAC_Init(DAC_HandleTypeDef *h) { assert(h->Instance==DAC);++initializations;return HAL_OK; }
int HAL_DAC_ConfigChannel(DAC_HandleTypeDef *h,const DAC_ChannelConfTypeDef *c,uint32_t ch)
{ assert(h->Instance==DAC); chosen_trigger=c->DAC_Trigger; chosen_channel=ch;buffer_on=c->DAC_OutputBuffer==DAC_OUTPUTBUFFER_ENABLE;return fail_config?HAL_ERROR:HAL_OK; }
int HAL_DAC_SetValue(DAC_HandleTypeDef *h,uint32_t ch,uint32_t align,uint32_t value)
{ assert(h->Instance==DAC && align==DAC_ALIGN_12B_R);chosen_channel=ch;written_value=value;return HAL_OK; }
uint32_t HAL_DAC_GetValue(DAC_HandleTypeDef *h,uint32_t ch) { assert(h->Instance==DAC);chosen_channel=ch;return written_value; }
int HAL_DACEx_TriangleWaveGenerate(DAC_HandleTypeDef *h,uint32_t ch,uint32_t amplitude)
{ assert(h->Instance==DAC);chosen_channel=ch;wave_mode=2U;wave_bits=amplitude;return HAL_OK; }
int HAL_DACEx_NoiseWaveGenerate(DAC_HandleTypeDef *h,uint32_t ch,uint32_t amplitude)
{ assert(h->Instance==DAC);chosen_channel=ch;wave_mode=1U;wave_bits=amplitude;return HAL_OK; }
#else
#include "gd32f10x.h"
#include "gd32f10x_dac.h"
void rcu_periph_clock_enable(uint32_t c) { assert(c==RCU_DAC);++clocks; }
void dac_enable(uint32_t d,uint8_t ch){assert(d==DAC0);enabled|=1U<<ch;}
void dac_disable(uint32_t d,uint8_t ch){assert(d==DAC0);disabled|=1U<<ch;}
void dac_dma_disable(uint32_t d,uint8_t ch){assert(d==DAC0);chosen_channel=ch;dma_enabled &= ~(1U << ch);}
void dac_dma_enable(uint32_t d,uint8_t ch){assert(d==DAC0);dma_enabled |= 1U << ch;}
void dac_trigger_disable(uint32_t d,uint8_t ch){assert(d==DAC0);chosen_channel=ch;++trigger_off;}
void dac_trigger_enable(uint32_t d,uint8_t ch){assert(d==DAC0);chosen_channel=ch;++trigger_on;}
void dac_wave_mode_config(uint32_t d,uint8_t ch,uint32_t mode){assert(d==DAC0);chosen_channel=ch;wave_mode=mode>>6U;}
void dac_lfsr_noise_config(uint32_t d,uint8_t ch,uint32_t bits){assert(d==DAC0);chosen_channel=ch;wave_bits=bits;}
void dac_triangle_noise_config(uint32_t d,uint8_t ch,uint32_t bits){assert(d==DAC0);chosen_channel=ch;wave_bits=bits;}
void dac_output_buffer_enable(uint32_t d,uint8_t ch){assert(d==DAC0);chosen_channel=ch;buffer_on=1;}
void dac_output_buffer_disable(uint32_t d,uint8_t ch){assert(d==DAC0);chosen_channel=ch;buffer_on=0;}
void dac_trigger_source_config(uint32_t d,uint8_t ch,uint32_t t){assert(d==DAC0);chosen_channel=ch;chosen_trigger=t;}
void dac_data_set(uint32_t d,uint8_t ch,uint32_t align,uint16_t v){assert(d==DAC0&&align==DAC_ALIGN_12B_R);chosen_channel=ch;written_value=v;}
void dac_software_trigger_enable(uint32_t d,uint8_t ch){assert(d==DAC0);chosen_channel=ch;++triggers;}
uint16_t dac_output_value_get(uint32_t d,uint8_t ch){assert(d==DAC0);chosen_channel=ch;return written_value;}
#endif
int main(void)
{
 static const uint32_t expected[]={0U,7U,4U,1U,5U,3U,10U,2U,6U};
 ABoxDacConfig c={ABOX_DAC_IMMEDIATE,1U,17U,ABOX_DAC_WAVE_NONE,0U};
 for(unsigned i=0;i<9;i++) {
  c.trigger=(ABoxDacTrigger)i;c.output_buffer=i&1U;c.initial_value=17U+i;
  chosen_trigger=0U;assert(ABoxDacHw_Open(2U,&c)==ABOX_HAL_OK);
  assert(chosen_trigger==expected[i] && buffer_on==(i&1U) && written_value==17U+i);
#ifdef TEST_STM32
  assert(chosen_channel==DAC_CHANNEL_2 && initializations==1U);
#else
  assert(chosen_channel==DAC_OUT1 && trigger_off==i+1U && trigger_on==i);
#endif
 }
 c.wave=ABOX_DAC_WAVE_TRIANGLE;c.wave_bits=12U;
 assert(ABoxDacHw_Open(2U,&c)==ABOX_HAL_OK && wave_mode==2U && wave_bits==(11U<<8U));
 c.wave=ABOX_DAC_WAVE_NOISE;c.wave_bits=1U;
 assert(ABoxDacHw_Open(2U,&c)==ABOX_HAL_OK && wave_mode==1U && wave_bits==0U);
 ABoxDacHw_Enable(1);assert(enabled&1U);
 ABoxDacHw_Write(1,4095);assert(ABoxDacHw_ReadOutput(1)==4095U);
 ABoxDacHw_Trigger(1);
#ifdef TEST_STM32
 assert(dac_registers.SWTRIGR==1U);fail_config=1;assert(ABoxDacHw_Open(1,&c)==ABOX_HAL_ERROR);
#else
 assert(triggers==1U);
#endif
 ABoxDacHw_DmaRequest(1,1);ABoxDacHw_DmaRequest(2,1);
#ifdef TEST_STM32
 assert((dac_registers.CR & (1U<<12)) && (dac_registers.CR & (1U<<28)));
 assert(ABoxDacHw_DmaAddress(1)==(uintptr_t)&dac_registers.DHR12R1);
 assert(ABoxDacHw_DmaAddress(2)==(uintptr_t)&dac_registers.DHR12R2);
#else
 assert(dma_enabled==3U);
 assert(ABoxDacHw_DmaAddress(1)==(uintptr_t)&fake_dac_data[0]);
 assert(ABoxDacHw_DmaAddress(2)==(uintptr_t)&fake_dac_data[1]);
#endif
 ABoxDacHw_DmaRequest(1,0);ABoxDacHw_Disable(1);assert(disabled&1U);return 0;
}
