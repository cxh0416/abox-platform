#include <assert.h>
#include <stdint.h>
#include "abox_time.h"
#include "abox_platform_port.h"
static uint32_t tick;
static uint64_t uptime;
static unsigned bound;
static uint32_t get_tick(void *ctx) { assert(ctx==(void *)0x1234);return tick++; }
static uint64_t get_monotonic(void *ctx) { assert(ctx==(void *)0x1234);return uptime; }
static ABoxPlatformPort port={.context=(void *)0x1234,.get_tick_ms=get_tick,.get_monotonic_ms=get_monotonic};
const ABoxPlatformPort *ABox_PlatformPortGet(void) { return bound?&port:0; }
int main(void)
{
    uint32_t a=7U;uint64_t b=9U;
    assert(ABoxTime_TickMs(0)==ABOX_HAL_INVALID && ABoxTime_MonotonicMs(0)==ABOX_HAL_INVALID);
    assert(ABoxTime_TickMs(&a)==ABOX_HAL_UNSUPPORTED && a==7U);
    assert(ABoxTime_MonotonicMs(&b)==ABOX_HAL_UNSUPPORTED && b==9U);
    bound=1;tick=0xfffffffeU;uptime=0x100000000ULL;
    assert(ABoxTime_TickMs(&a)==ABOX_HAL_OK && a==0xfffffffeU);
    assert(ABoxTime_DelayMs(3U)==ABOX_HAL_OK && tick==3U);
    assert(ABoxTime_MonotonicMs(&b)==ABOX_HAL_OK && b==0x100000000ULL);
    port.get_monotonic_ms=0;b=9U;
    assert(ABoxTime_MonotonicMs(&b)==ABOX_HAL_UNSUPPORTED && b==9U);
    return 0;
}
