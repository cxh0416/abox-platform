#ifndef ABOX_EC800_DIAGNOSTICS_H
#define ABOX_EC800_DIAGNOSTICS_H
#include "abox_ec800_at.h"
#include <stddef.h>

#define ABOX_EC800_DIAG_QUERY_TIMEOUT_MS 3000U
#define ABOX_EC800_DIAG_RETRY_MS 15000U
#define ABOX_EC800_DIAG_VALID_MS 60000U

typedef enum {
    ABOX_EC800_DIAG_UNKNOWN = 0, ABOX_EC800_DIAG_SIM_NOT_INSERTED,
    ABOX_EC800_DIAG_SIM_PIN_REQUIRED, ABOX_EC800_DIAG_SIM_ERROR,
    ABOX_EC800_DIAG_SIM_READY, ABOX_EC800_DIAG_NETWORK_NOT_REGISTERED,
    ABOX_EC800_DIAG_DATA_NOT_READY, ABOX_EC800_DIAG_MODEM_NO_RESPONSE,
    ABOX_EC800_DIAG_MQTT_READY
} ABoxEc800DiagState;
typedef enum {
    ABOX_EC800_FIRMWARE_UNKNOWN = 0,
    ABOX_EC800_FIRMWARE_A10,
    ABOX_EC800_FIRMWARE_A18
} ABoxEc800Firmware;
typedef struct {
    ABoxEc800DiagState state;
    uint32_t updated_at_ms;
    uint16_t last_cme;
    uint8_t valid, registration, last_result;
} ABoxEc800DiagSnapshot;
typedef struct {
    ABoxEc800At *at;
    ABoxEc800Owner owner;
    ABoxEc800DiagSnapshot snapshot;
    uint32_t checked_at_ms, due_ms, firmware_due_ms;
    uint8_t pending, step, observed, sim, registration_valid;
    uint8_t firmware_probe_enabled, firmware, firmware_candidate;
} ABoxEc800Diagnostics;

/* Poll/get/reset in the existing UART owner task. No GPIO, reset request,
 * dynamic allocation or additional transport. Idle-only NORMAL submission.
 * attached/pdp_active come from existing MQTT CGATT/QIACT results. */
int ABoxEc800Diagnostics_Init(ABoxEc800Diagnostics *, ABoxEc800At *, ABoxEc800Owner);
/* Contract values are owned by the product. NULL means identification is pending. */
static inline const char *ABoxEc800Firmware_Contract(ABoxEc800Firmware firmware,
                                                     const char *a10, const char *a18)
{
    return firmware == ABOX_EC800_FIRMWARE_A10 ? a10 :
           firmware == ABOX_EC800_FIRMWARE_A18 ? a18 : NULL;
}
void ABoxEc800Diagnostics_EnableFirmwareProbe(ABoxEc800Diagnostics *);
ABoxEc800Firmware ABoxEc800Diagnostics_Firmware(const ABoxEc800Diagnostics *);
void ABoxEc800Diagnostics_Poll(ABoxEc800Diagnostics *, uint32_t now_ms,
    uint8_t may_query, uint8_t attached, uint8_t pdp_active, uint8_t mqtt_ready);
void ABoxEc800Diagnostics_OnModemReset(ABoxEc800Diagnostics *);
void ABoxEc800Diagnostics_Get(const ABoxEc800Diagnostics *, ABoxEc800DiagSnapshot *);
#endif
