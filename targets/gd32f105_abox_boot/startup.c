#include <stdint.h>
#include "gd32f10x.h"
#include "abox_boot_v2.h"

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern void __libc_init_array(void);
extern int main(void);
void Reset_Handler(void);
void SysTick_Handler(void);
static void Default_Handler(void) { for (;;) {} }
static void HardFault_Handler(void) { ABoxBootV2_FaultRecord(1U, 0U, 0U); NVIC_SystemReset(); }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used))
const uintptr_t boot_vectors[16U + 68U] = {
    [0] = (uintptr_t)&_estack, [1] = (uintptr_t)Reset_Handler,
    [2 ... 16U + 67U] = (uintptr_t)Default_Handler,
    [3] = (uintptr_t)HardFault_Handler, [15] = (uintptr_t)SysTick_Handler
};
#pragma GCC diagnostic pop
void Reset_Handler(void)
{
    uint32_t *source = &_sidata;
    for (uint32_t *p = &_sdata; p < &_edata; ++p) *p = *source++;
    for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0U;
    __libc_init_array();
    (void)main();
    for (;;) {}
}
