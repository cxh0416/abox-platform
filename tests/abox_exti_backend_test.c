#include <assert.h>
#include "abox_exti_backend.h"
#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
ExtiRegisters exti_regs;
AfioRegisters afio_regs;
unsigned afio_clocks;
#define INTEN exti_regs.IMR
#define EVEN exti_regs.EMR
#define SWIE exti_regs.SWIER
#define RTEN exti_regs.RTSR
#define FTEN exti_regs.FTSR
#define PENDING exti_regs.PR
#else
#include "gd32f10x.h"
#include "gd32f10x_exti.h"
uint32_t exti_inten, exti_even, exti_rten, exti_ften, exti_pd,exti_swie;
unsigned afio_clocks, selected_port, selected_pin;
void rcu_periph_clock_enable(uint32_t clock) { assert(clock==RCU_AF); ++afio_clocks; }
void gpio_exti_source_select(uint8_t port,uint8_t pin) { selected_port=port; selected_pin=pin; }
#define INTEN exti_inten
#define EVEN exti_even
#define SWIE exti_swie
#define RTEN exti_rten
#define FTEN exti_ften
#define PENDING exti_pd
#endif
int main(void)
{
    ABoxGpioPin pin={ABOX_GPIO_D,7}; uint32_t bit=1U<<7;
    INTEN=1U<<8; RTEN=1U<<8; FTEN=1U<<8;
    ABoxExtiHw_Open(pin,ABOX_EXTI_BOTH,ABOX_EXTI_INTERRUPT|ABOX_EXTI_EVENT);
    assert(afio_clocks==1U && INTEN==(bit|(1U<<8)) && RTEN==(bit|(1U<<8)) && FTEN==(bit|(1U<<8)));
    assert(EVEN==bit);ABoxExtiHw_Trigger(7U);assert(SWIE==bit);
#ifdef TEST_STM32
    assert(((afio_regs.EXTICR[1] >> 12U) & 15U)==ABOX_GPIO_D);
#else
    assert(selected_port==ABOX_GPIO_D && selected_pin==7U);
#endif
    assert(PENDING==bit); PENDING=bit|(1U<<8);
    assert(ABoxExtiHw_Pending(bit)==bit);
    ABoxExtiHw_Clear(7); assert(PENDING==bit);
    ABoxExtiHw_Close(7); assert(INTEN==(1U<<8) && !EVEN && RTEN==(1U<<8) && FTEN==(1U<<8) && PENDING==bit);
    ABoxExtiHw_Open(pin,ABOX_EXTI_FALLING,ABOX_EXTI_EVENT); assert(!(RTEN&bit) && (FTEN&bit) && !(INTEN&bit) && (EVEN&bit));
    ABoxExtiHw_Open(pin,ABOX_EXTI_RISING,ABOX_EXTI_INTERRUPT); assert((RTEN&bit) && !(FTEN&bit) && (INTEN&bit) && !(EVEN&bit));
    ABoxExtiHw_OpenInternal(17U,ABOX_EXTI_RISING,ABOX_EXTI_INTERRUPT|ABOX_EXTI_EVENT);
    assert((INTEN&(1U<<17)) && (EVEN&(1U<<17)) && (RTEN&(1U<<17)) && !(FTEN&(1U<<17)));
    PENDING=1U<<17;assert(ABoxExtiHw_RawPending(1U<<17)==(1U<<17) && ABoxExtiHw_EnabledPending(1U<<17)==(1U<<17));
    ABoxExtiHw_Close(17U);assert(!(INTEN&(1U<<17)) && !(EVEN&(1U<<17)));
    return 0;
}
