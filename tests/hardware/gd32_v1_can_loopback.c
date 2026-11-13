#include <stdint.h>
#include <string.h>
#include "gd32f10x.h"
#include "abox_can_port.h"

extern uint32_t _estack, _sbss, _ebss;
void Reset_Handler(void);
static void Default_Handler(void) { for (;;) {} }
uint32_t SystemCoreClock = 8000000U;
volatile uint32_t g_can_bench_result[9];

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used))
const uintptr_t can_bench_vectors[16U + 68U] = {
    [0] = (uintptr_t)&_estack, [1] = (uintptr_t)Reset_Handler,
    [2 ... 16U + 67U] = (uintptr_t)Default_Handler
};
#pragma GCC diagnostic pop

static int clock_init(void)
{
    uint32_t timeout;
    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV1);
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV1);
    rcu_pll_config(RCU_PLLSRC_IRC8M_DIV2, RCU_PLL_MUL9);
    rcu_osci_on(RCU_PLL_CK);
    if (rcu_osci_stab_wait(RCU_PLL_CK) != SUCCESS) return 0;
    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    for (timeout = 0U; timeout < 100000U; ++timeout)
        if (rcu_system_clock_source_get() == RCU_SCSS_PLL) break;
    if (timeout == 100000U || rcu_clock_freq_get(CK_APB1) != 36000000U) return 0;
    SystemCoreClock = 36000000U; return 1;
}

static int loop(unsigned bus, uint32_t id, uint8_t extended, uint8_t remote)
{
    ABoxCanFrame out = {0}, in;
    int result = 0;
    out.id = id; out.extended = extended; out.remote = remote; out.length = 8U;
    for (unsigned i = 0U; i < 8U; ++i) out.data[i] = (uint8_t)(0xA0U + i);
    if (ABoxCanPort_Send(bus, &out) != 1) return 0;
    for (uint32_t i = 0U; i < 2000000U; ++i) {
        result = ABoxCanPort_Receive(bus, &in);
        if (result != 0) break;
    }
    if (result != 1 || in.id != id || in.extended != extended || in.remote != remote || in.length != 8U) return 0;
    return remote || !memcmp(in.data, out.data, 8U);
}

void Reset_Handler(void)
{
    for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0U;
    g_can_bench_result[0] = (uint32_t)clock_init();
    if (g_can_bench_result[0]) {
        g_can_bench_result[1] = (uint32_t)ABoxCanPort_Init(0U, 250000U, ABOX_CAN_SILENT_LOOPBACK);
        if (g_can_bench_result[1]) g_can_bench_result[2] = (uint32_t)loop(0U, 0x123U, 0U, 0U);
        g_can_bench_result[3] = (uint32_t)ABoxCanPort_Init(1U, 500000U, ABOX_CAN_SILENT_LOOPBACK);
        if (g_can_bench_result[3]) {
            g_can_bench_result[4] = (uint32_t)loop(1U, 0x1234567U, 1U, 0U);
            g_can_bench_result[5] = (uint32_t)loop(1U, 0x456U, 0U, 1U);
            g_can_bench_result[6] = (uint32_t)loop(0U, 0x321U, 0U, 0U);
            g_can_bench_result[7] = (uint32_t)(!ABoxCanPort_Stop(0U) && ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
        }
    }
    g_can_bench_result[8] = 0xCA11BEEFU;
    for (;;) __WFI();
}
