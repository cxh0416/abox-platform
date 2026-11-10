#ifndef SWEEPER_TEST_OTA_CLOUD_MAINTENANCE_H
#define SWEEPER_TEST_OTA_CLOUD_MAINTENANCE_H
#include <stdint.h>
/* Startup failure-injection fixture: no concurrent maintenance transaction. */
static inline uint8_t CloudMaintenance_ServiceBusy(void) { return 0U; }
#endif
