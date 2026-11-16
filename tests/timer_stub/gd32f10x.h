#ifndef TEST_TIMER_GD_H
#define TEST_TIMER_GD_H
#include <stdint.h>
typedef enum { RESET=0, SET=1 } FlagStatus;
typedef enum { DISABLE=0, ENABLE=1 } ControlStatus;
typedef unsigned rcu_periph_enum;
typedef unsigned rcu_periph_reset_enum;
#define RCU_TIMER0RST 1U
#define RCU_TIMER1RST 2U
#define RCU_TIMER2RST 3U
#define RCU_TIMER3RST 4U
#define RCU_TIMER4RST 5U
#define RCU_TIMER5RST 6U
#define RCU_TIMER6RST 7U
void rcu_periph_reset_enable(rcu_periph_reset_enum id);
void rcu_periph_reset_disable(rcu_periph_reset_enum id);
extern uint32_t fake_timer_crep[8],fake_timer_swevg[8];
extern uint32_t fake_timer_dmacfg[8],fake_timer_dmatb[8];
#define TIMER_DMACFG(id) fake_timer_dmacfg[id]
#define TIMER_DMATB(id) fake_timer_dmatb[id]
#define TIMER_CREP(id) fake_timer_crep[id]
#define TIMER_SWEVG(id) fake_timer_swevg[id]
#define TIMER_SWEVG_UPG 1U
typedef struct { uint16_t prescaler,alignedmode,counterdirection,clockdivision; uint32_t period; uint8_t repetitioncounter; } timer_parameter_struct;
typedef struct { uint16_t outputstate,outputnstate,ocpolarity,ocnpolarity,ocidlestate,ocnidlestate; } timer_oc_parameter_struct;
typedef struct { uint16_t icpolarity,icselection,icprescaler,icfilter; } timer_ic_parameter_struct;
#define TIMER_DMA_CH0D (1U << 9U)
#define TIMER_DMA_UPD (1U << 8U)
extern uint32_t fake_timer_dma_capture[8][4];
extern uint32_t fake_timer_ctl0[8],fake_timer_ctl1[8],fake_timer_chctl2[8],fake_timer_cchp[8],fake_timer_smcfg[8];
#define TIMER_CTL0(id) fake_timer_ctl0[id]
#define TIMER_CTL0_SPM (1U << 3U)
#define TIMER_CTL1(id) fake_timer_ctl1[id]
#define TIMER_CTL1_TI0S (1U << 7U)
#define TIMER_CHCTL2(id) fake_timer_chctl2[id]
#define TIMER_CCHP(id) fake_timer_cchp[id]
#define TIMER_SMCFG(id) fake_timer_smcfg[id]
#define TIMER_SMCFG_SMC 7U
#define TIMER_CHCTL2_CH0NEN (1U<<2U)
#define TIMER_CHCTL2_CH0NP (1U<<3U)
#define TIMER_CCHP_POEN (1U<<15U)
#define TIMER_FLAG_BRK (1U<<7U)
#define TIMER_INT_BRK (1U<<7U)
#define TIMER_INT_FLAG_BRK TIMER_INT_BRK
#define TIMER_CH0CV(id) fake_timer_dma_capture[(id)][0]
#define TIMER0 1U
#define TIMER1 2U
#define TIMER2 3U
#define TIMER3 4U
#define TIMER4 5U
#define TIMER5 6U
#define TIMER6 7U
#define RCU_TIMER0 1U
#define RCU_TIMER1 2U
#define RCU_TIMER2 3U
#define RCU_TIMER3 4U
#define RCU_TIMER4 5U
#define RCU_TIMER5 6U
#define RCU_TIMER6 7U
#define TIMER_COUNTER_UP 0U
#define TIMER_COUNTER_DOWN 1U
#define TIMER_COUNTER_EDGE 0U
#define TIMER_COUNTER_CENTER_DOWN 2U
#define TIMER_COUNTER_CENTER_UP 3U
#define TIMER_COUNTER_CENTER_BOTH 4U
#define TIMER_CKDIV_DIV1 0U
#define TIMER_FLAG_UP 1U
#define TIMER_INT_UP 1U
#define TIMER_INT_FLAG_UP 1U
#define TIMER_FLAG_CH0 2U
#define TIMER_FLAG_CH0O 512U
#define TIMER_INT_CH0 2U
#define TIMER_INT_FLAG_CH0 2U
#define TIMER_IC_POLARITY_RISING 0U
#define TIMER_IC_POLARITY_FALLING 2U
#define TIMER_IC_SELECTION_DIRECTTI 1U
#define TIMER_IC_SELECTION_INDIRECTTI 2U
#define TIMER_IC_SELECTION_ITS 3U
#define TIMER_IC_PSC_DIV1 0U
#define TIMER_IC_PSC_DIV2 4U
#define TIMER_IC_PSC_DIV4 8U
#define TIMER_IC_PSC_DIV8 12U
#define TIMER_QUAD_DECODER_MODE0 1U
#define TIMER_QUAD_DECODER_MODE1 2U
#define TIMER_QUAD_DECODER_MODE2 3U
#define TIMER_CCX_ENABLE 1U
#define TIMER_CCX_DISABLE 0U
#define TIMER_OC_POLARITY_HIGH 0U
#define TIMER_OC_POLARITY_LOW 1U
#define TIMER_OC_MODE_LOW 1U
#define TIMER_OC_MODE_HIGH 2U
#define TIMER_OC_MODE_PWM0 3U
#define TIMER_OC_MODE_TIMING 4U
#define TIMER_OC_MODE_ACTIVE 5U
#define TIMER_OC_MODE_INACTIVE 6U
#define TIMER_OC_MODE_TOGGLE 7U
#define TIMER_OC_SHADOW_DISABLE 0U
#define TIMER_OC_SHADOW_ENABLE 8U
void rcu_periph_clock_enable(rcu_periph_enum id);
void rcu_periph_clock_disable(rcu_periph_enum id);
void timer_deinit(uint32_t id);
void timer_struct_para_init(timer_parameter_struct *p);
void timer_init(uint32_t id,timer_parameter_struct *p);
void timer_flag_clear(uint32_t id,uint32_t flag);
FlagStatus timer_flag_get(uint32_t id,uint32_t flag);
void timer_channel_input_struct_para_init(timer_ic_parameter_struct *p);
void timer_input_capture_config(uint32_t id,uint16_t ch,timer_ic_parameter_struct *p);
uint32_t timer_channel_capture_value_register_read(uint32_t id,uint16_t ch);
void timer_quadrature_decoder_mode_config(uint32_t id,uint32_t mode,uint16_t a,uint16_t b);
void timer_internal_clock_config(uint32_t id);
void timer_interrupt_enable(uint32_t id,uint32_t flag);
void timer_interrupt_disable(uint32_t id,uint32_t flag);
void timer_master_output_trigger_source_select(uint32_t id,uint32_t value);
void timer_dma_enable(uint32_t id,uint16_t flag);
void timer_dma_disable(uint32_t id,uint16_t flag);
void timer_enable(uint32_t id);
void timer_disable(uint32_t id);
uint32_t timer_counter_read(uint32_t id);
void timer_counter_value_config(uint32_t id,uint32_t counter);
FlagStatus timer_interrupt_flag_get(uint32_t id,uint32_t flag);
void timer_interrupt_flag_clear(uint32_t id,uint32_t flag);
void timer_channel_output_struct_para_init(timer_oc_parameter_struct *p);
void timer_channel_output_config(uint32_t id,uint16_t ch,timer_oc_parameter_struct *p);
void timer_channel_output_mode_config(uint32_t id,uint16_t ch,uint16_t mode);
void timer_channel_output_pulse_value_config(uint32_t id,uint16_t ch,uint16_t pulse);
void timer_channel_output_shadow_config(uint32_t id,uint16_t ch,uint16_t shadow);
void timer_channel_output_state_config(uint32_t id,uint16_t ch,uint32_t state);
void timer_primary_output_config(uint32_t id,ControlStatus enabled);
#endif
