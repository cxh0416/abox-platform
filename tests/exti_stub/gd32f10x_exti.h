#ifndef GD32F10X_EXTI_H
#define GD32F10X_EXTI_H
#include <stdint.h>
extern uint32_t exti_inten, exti_even, exti_rten, exti_ften, exti_pd,exti_swie;
#define EXTI_INTEN exti_inten
#define EXTI_EVEN exti_even
#define EXTI_SWIEV exti_swie
#define EXTI_RTEN exti_rten
#define EXTI_FTEN exti_ften
#define EXTI_PD exti_pd
#endif
