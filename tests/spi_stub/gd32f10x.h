#ifndef TEST_SPI_GD_H
#define TEST_SPI_GD_H
#include <stdint.h>
extern uint32_t fake_clock_regs[12];
#define RCU ((uintptr_t)fake_clock_regs)
#define HXTAL_VALUE 8000000U
typedef enum { RESET = 0, SET = 1 } FlagStatus;
#define SPI0 1U
#define SPI1 2U
#define SPI2 3U
#define RCU_SPI0 1U
#define RCU_SPI1 2U
#define RCU_SPI2 3U
void rcu_periph_clock_enable(uint32_t id);
void rcu_periph_clock_disable(uint32_t id);
#endif
