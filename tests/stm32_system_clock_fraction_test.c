#include <assert.h>
#include "stm32f1xx.h"
TestRcc test_rcc;
extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);
int main(void)
{
    test_rcc.CFGR=(2U<<2)|(4U<<18)|RCC_CFGR_PLLSRC; /* 25 MHz /3 *6 */
    test_rcc.CFGR2=2U;
    SystemCoreClockUpdate();assert(SystemCoreClock==50000000U);
    /* 25 MHz /6 *12 /5 *6 */
    test_rcc.CFGR2=RCC_CFGR2_PREDIV1SRC|4U|(5U<<4)|(10U<<8);
    SystemCoreClockUpdate();assert(SystemCoreClock==60000000U);
    return 0;
}
