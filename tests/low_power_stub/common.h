#include <stdint.h>
typedef struct { uint32_t SCR; } FakeScb;
extern FakeScb fake_scb;
extern uint32_t fake_power[2],fake_ipsr,fake_primask,fake_basepri;
extern unsigned fake_waits,fake_events,fake_clocks;extern uint32_t sleep_scr,sleep_ctl;
extern uint32_t fake_rcc_regs[2],fake_rcc_source;
#define fake_rcc_ctl fake_rcc_regs[0]
#define fake_rcc_cfg fake_rcc_regs[1]
#define SCB (&fake_scb)
#define SCB_SCR_SLEEPDEEP_Msk 4U
#define __get_IPSR() fake_ipsr
#define __get_PRIMASK() fake_primask
#define __get_BASEPRI() fake_basepri
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __WFI() do { ++fake_waits;sleep_scr=SCB->SCR;sleep_ctl=fake_power[0]; } while (0)
#define __WFE() __WFI()
#define __SEV() (++fake_events)
