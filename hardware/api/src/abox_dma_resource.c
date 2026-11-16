#include "abox_dma_resource.h"
#include "abox_critical.h"
#include <stdint.h>
static uint8_t claimed[2];
static int valid(unsigned controller, unsigned channel)
{ return controller >= 1U && controller <= 2U && channel >= 1U && channel <= (controller == 1U ? 7U : 5U); }
int ABoxDmaResource_Claim(unsigned controller, unsigned channel)
{
    uint32_t key;
    uint8_t bit;
    if (!valid(controller, channel)) return 0;
    bit = (uint8_t)(1U << (channel - 1U));
    key = ABoxCritical_Enter();
    if (claimed[controller - 1U] & bit) { ABoxCritical_Exit(key); return 0; }
    claimed[controller - 1U] |= bit;
    ABoxCritical_Exit(key);
    return 1;
}
void ABoxDmaResource_Release(unsigned controller, unsigned channel)
{
    uint32_t key;
    if (!valid(controller, channel)) return;
    key = ABoxCritical_Enter();
    claimed[controller - 1U] &= (uint8_t)~(1U << (channel - 1U));
    ABoxCritical_Exit(key);
}
