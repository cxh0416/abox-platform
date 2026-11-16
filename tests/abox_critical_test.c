#include <assert.h>
#include <stdint.h>
#include "abox_critical.h"
static uint32_t mask;
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1U; }
void __set_PRIMASK(uint32_t key) { mask=key; }
int main(void)
{
    uint32_t first=ABoxCritical_Enter(), second=ABoxCritical_Enter();
    assert(first==0U && second==1U && mask==1U);
    ABoxCritical_Exit(second);assert(mask==1U);
    ABoxCritical_Exit(first);assert(mask==0U);
    mask=1U;first=ABoxCritical_Enter();ABoxCritical_Exit(first);assert(mask==1U);
    return 0;
}
