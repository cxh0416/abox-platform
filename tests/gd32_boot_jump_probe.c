#include "abox_boot_v2.h"
/* Bench only: exercise the existing no-state vector jump without state writes. */
int __wrap_ABoxBootV2_StateLoad(ABoxBootV2Record *record)
{ (void)record; return 0; }
