#include <stdint.h>
#include "gd32f10x.h"
#include "abox_boot_v2.h"
#include "abox_gd32_modem.h"
#include "abox_gd32_monotonic.h"
#include "abox_gd32_serial.h"

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern void __libc_init_array(void);
extern int main(void);
void Reset_Handler(void);
void Default_Handler(void) { for (;;) {} }
void HardFault_Handler(void) { ABoxBootV2_FaultRecord(1U, 0U, 0U); NVIC_SystemReset(); }
void DMA0_Channel4_IRQHandler(void) { ABoxGdModem_DmaIrq(); }
void TIMER1_IRQHandler(void) { ABoxGdMonotonic_IrqHandler(); }
void USART0_IRQHandler(void) { ABoxGdModem_UsartIrq(); }
void UART4_IRQHandler(void) { ABoxGdSerial_IrqHandler(); }

/* GD32F10x_CL IRQ indices come from the pinned vendor device header. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used))
const uintptr_t g_pfnVectors[16U + 68U] = {
    [0] = (uintptr_t)&_estack, [1] = (uintptr_t)Reset_Handler,
    [2 ... 16U + 67U] = (uintptr_t)Default_Handler,
    [3] = (uintptr_t)HardFault_Handler,
    [16U + DMA0_Channel4_IRQn] = (uintptr_t)DMA0_Channel4_IRQHandler,
    [16U + TIMER1_IRQn] = (uintptr_t)TIMER1_IRQHandler,
    [16U + USART0_IRQn] = (uintptr_t)USART0_IRQHandler,
    [16U + UART4_IRQn] = (uintptr_t)UART4_IRQHandler
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
