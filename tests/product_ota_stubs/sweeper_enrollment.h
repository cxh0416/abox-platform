#ifndef SWEEPER_TEST_OTA_ENROLLMENT_H
#define SWEEPER_TEST_OTA_ENROLLMENT_H
#include <stdint.h>
/* Startup failure-injection fixture: no concurrent enrollment HTTP request. */
static inline uint8_t SweeperEnrollment_HttpBusy(void) { return 0U; }
#endif
