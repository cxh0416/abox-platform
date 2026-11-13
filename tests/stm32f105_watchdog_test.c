#include <assert.h>
#include "iwdg.h"
#include "abox_watchdog_port.h"
IWDG_HandleTypeDef hiwdg;
static unsigned init_calls, refresh_calls, frozen;
static int fail_init;
int HAL_IWDG_Init(IWDG_HandleTypeDef *h)
{ assert(h == &hiwdg); ++init_calls; return fail_init; }
int HAL_IWDG_Refresh(IWDG_HandleTypeDef *h)
{ assert(h == &hiwdg); ++refresh_calls; return HAL_OK; }
void test_freeze(void) { ++frozen; }
int main(void)
{
    assert(!ABoxWatchdog_Refresh());
    assert(!ABoxWatchdog_Init(7U, 1250U) && !init_calls);
    assert(!ABoxWatchdog_Init(64U, 4096U) && !init_calls);
    fail_init = 1; assert(!ABoxWatchdog_Init(64U, 1250U));
    fail_init = 0; assert(ABoxWatchdog_Init(64U, 1250U));
    assert(hiwdg.Instance == IWDG && hiwdg.Init.Prescaler == IWDG_PRESCALER_64 && hiwdg.Init.Reload == 1250U);
    assert(!ABoxWatchdog_Init(64U, 1250U));
    assert(ABoxWatchdog_Refresh() && refresh_calls == 1U);
    ABoxWatchdog_FreezeWhileDebugging(); assert(frozen == 1U);
    return 0;
}
