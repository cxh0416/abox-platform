#include "abox_pvd.h"
#include "abox_pvd_backend.h"
#include <assert.h>
#include <stddef.h>
static int enabled,pending,level;
static unsigned clocks,configured,closed,clears,events,locks;
static uint8_t seen;
uint32_t ABoxCritical_Enter(void) { ++locks;return 7U; }
void ABoxCritical_Exit(uint32_t key) { assert(key==7U && locks);--locks; }
void ABoxPvdHw_EnableClock(void) { ++clocks; }
int ABoxPvdHw_IsEnabled(void) { return enabled; }
void ABoxPvdHw_Configure(const ABoxPvdConfig *c) { assert(c);enabled=1;++configured; }
void ABoxPvdHw_Disable(void) { enabled=0;++closed; }
uint8_t ABoxPvdHw_Read(void) { return (uint8_t)level; }
uint8_t ABoxPvdHw_Pending(void) { return (uint8_t)pending; }
void ABoxPvdHw_Clear(void) { pending=0;++clears; }
static void changed(void *ctx,uint8_t below) { assert(ctx==(void *)1);++events;seen=below; }
int main(void)
{
    ABoxPvdConfig c={ABOX_PVD_2V7,ABOX_PVD_BOTH,1U,0U};ABoxPvdEvents e={(void *)1,changed};uint8_t v=2U;
    assert(ABoxPvd_Open(NULL,&e)==ABOX_HAL_INVALID && !clocks);
    c.level=(ABoxPvdLevel)8;assert(ABoxPvd_Open(&c,&e)==ABOX_HAL_INVALID);c.level=ABOX_PVD_2V7;
    enabled=1;assert(ABoxPvd_Open(&c,&e)==ABOX_HAL_BUSY && !configured);enabled=0;
    assert(ABoxPvd_Open(&c,&e)==ABOX_HAL_OK && configured==1U);
    assert(ABoxPvd_Open(&c,&e)==ABOX_HAL_BUSY);
    assert(ABoxPvd_Read(&v)==ABOX_HAL_OK && !v);
    pending=1;level=1;ABoxPvd_Irq();assert(events==1U && seen==1U && !pending && clears==1U);
    assert(ABoxPvd_Pending(&v)==ABOX_HAL_OK && !v);
    assert(ABoxPvd_Close()==ABOX_HAL_OK && closed==1U);
    c.interrupt=0U;c.event=1U;assert(ABoxPvd_Open(&c,&e)==ABOX_HAL_INVALID);
    assert(ABoxPvd_Open(&c,NULL)==ABOX_HAL_OK);pending=1;ABoxPvd_Irq();assert(pending && events==1U);
    assert(ABoxPvd_Clear()==ABOX_HAL_OK && !pending && ABoxPvd_Close()==ABOX_HAL_OK && !locks);
    return 0;
}
