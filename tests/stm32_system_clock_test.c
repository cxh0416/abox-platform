#include <assert.h>
#include "stm32f1xx.h"
TestRcc test_rcc;
extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);
int main(void)
{
    test_rcc.CFGR=(2U<<2)|(13U<<18); /* HSI/2 x 6.5 */
    SystemCoreClockUpdate();assert(SystemCoreClock==26000000U);
    test_rcc.CFGR|=RCC_CFGR_PLLSRC; /* HSE x 6.5 */
    SystemCoreClockUpdate();assert(SystemCoreClock==52000000U);
    test_rcc.CFGR|=8U<<4; /* AHB /2 */
    SystemCoreClockUpdate();assert(SystemCoreClock==26000000U);
    test_rcc.CFGR=(2U<<2)|(7U<<18)|RCC_CFGR_PLLSRC; /* HSE x 9 */
    SystemCoreClockUpdate();assert(SystemCoreClock==72000000U);
    test_rcc.CFGR2=RCC_CFGR2_PREDIV1SRC | (9U) | (1U<<4) | (15U<<8); /* HSE /2 x20 /10 */
    SystemCoreClockUpdate();assert(SystemCoreClock==72000000U);
    return 0;
}
