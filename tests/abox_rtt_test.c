#include "SEGGER_RTT.h"

#include <assert.h>
#include <string.h>

/* SEGGER selects the embOS simulator lock on Windows. This host fixture is
 * single-threaded; actual target locking is left to SEGGER/product settings. */
#if defined(WIN32)
void OS_SIM_EnterCriticalSection(void) {}
void OS_SIM_LeaveCriticalSection(void) {}
#endif

int main(void)
{
    char fill[BUFFER_SIZE_UP - 1];
    memset(fill, 'x', sizeof(fill));
    SEGGER_RTT_Init();
    assert(_SEGGER_RTT.aUp[0].Flags == SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    assert(SEGGER_RTT_Write(0, "abc", 3) == 3);
    assert(memcmp(_SEGGER_RTT.aUp[0].pBuffer, "abc", 3) == 0);
    assert(SEGGER_RTT_Write(0, fill, sizeof(fill)) == 0);
    assert(_SEGGER_RTT.aUp[0].WrOff == 3);
    _SEGGER_RTT.aUp[0].RdOff = 3; /* Simulate the debugger consuming data. */
    assert(SEGGER_RTT_Write(0, fill, sizeof(fill)) == sizeof(fill));
    assert(SEGGER_RTT_Write(0, "z", 1) == 0);
    return 0;
}
