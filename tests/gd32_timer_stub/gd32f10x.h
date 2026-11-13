#ifndef GD32_TIMER_TEST_STUB_H
#define GD32_TIMER_TEST_STUB_H
#include <stdint.h>
typedef enum { RESET = 0, SET = 1 } FlagStatus;
typedef struct { uint16_t prescaler, alignedmode, counterdirection; uint32_t period; uint16_t clockdivision; uint8_t repetitioncounter; } timer_parameter_struct;
#define CK_APB1 1U
#define RCU_TIMER1 1U
#define TIMER1 1U
#define TIMER1_IRQn 28
#define TIMER_COUNTER_EDGE 0U
#define TIMER_COUNTER_UP 0U
#define TIMER_CKDIV_DIV1 0U
#define TIMER_FLAG_UP 1U
#define TIMER_INT_UP 1U
extern uint32_t test_rcu_cfg0;
#define RCU_CFG0 test_rcu_cfg0
uint32_t rcu_clock_freq_get(uint32_t clock);
void rcu_periph_clock_enable(uint32_t peripheral);
void timer_deinit(uint32_t timer);
void timer_struct_para_init(timer_parameter_struct *config);
void timer_init(uint32_t timer, timer_parameter_struct *config);
void timer_flag_clear(uint32_t timer, uint32_t flag);
FlagStatus timer_flag_get(uint32_t timer, uint32_t flag);
uint32_t timer_counter_read(uint32_t timer);
void timer_interrupt_enable(uint32_t timer, uint32_t interrupt);
void timer_enable(uint32_t timer);
void NVIC_SetPriority(int irq, uint32_t priority);
void NVIC_ClearPendingIRQ(int irq);
void NVIC_EnableIRQ(int irq);
uint32_t test_get_primask(void);
void test_disable_irq(void);
void test_set_primask(uint32_t key);
#define __get_PRIMASK() test_get_primask()
#define __disable_irq() test_disable_irq()
#define __set_PRIMASK(key) test_set_primask(key)
#endif
