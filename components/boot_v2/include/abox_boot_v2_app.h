#ifndef ABOX_BOOT_V2_APP_H
#define ABOX_BOOT_V2_APP_H

#include <stdint.h>
#include "abox_boot_v2.h"
#include "abox_https_ufs_downloader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ABOX_BOOT_V2_APP_URL_SIZE 256U
#define ABOX_BOOT_V2_APP_VERSION_SIZE 32U
#define ABOX_BOOT_V2_APP_RAW_CHUNK 1024U

#define ABOX_BOOT_V2_APP_ERROR_UNSUPPORTED 1001U
#define ABOX_BOOT_V2_APP_ERROR_CA 1002U
#define ABOX_BOOT_V2_APP_ERROR_HTTP 1101U
#define ABOX_BOOT_V2_APP_ERROR_UFS 1201U
#define ABOX_BOOT_V2_APP_ERROR_SIZE 1301U
#define ABOX_BOOT_V2_APP_ERROR_CRC 1302U
#define ABOX_BOOT_V2_APP_ERROR_VECTOR 1303U
#define ABOX_BOOT_V2_APP_ERROR_STATE 1401U
#define ABOX_BOOT_V2_APP_ERROR_URL 1402U
#define ABOX_BOOT_V2_STABLE_SEED_SOURCE "local_flash"

typedef enum { ABOX_BOOT_V2_AT_OK=0, ABOX_BOOT_V2_AT_ERROR, ABOX_BOOT_V2_AT_TIMEOUT, ABOX_BOOT_V2_AT_SEND_OK, ABOX_BOOT_V2_AT_SEND_FAIL } ABoxBootV2AtResult;
typedef enum { ABOX_BOOT_V2_AT_LINE=0, ABOX_BOOT_V2_AT_RAW } ABoxBootV2AtEvent;
typedef void (*ABoxBootV2AtDoneFn)(ABoxBootV2AtResult result, void *user);
typedef void (*ABoxBootV2AtEventFn)(ABoxBootV2AtEvent event, const uint8_t *data, uint16_t length, void *user);

typedef struct {
    char url[ABOX_BOOT_V2_APP_URL_SIZE];
    char version[ABOX_BOOT_V2_APP_VERSION_SIZE];
    uint32_t size;
    uint32_t crc32;
} ABoxBootV2AppRequest;

typedef struct {
    void *context;
    uint32_t (*tick_ms)(void *context);
    int (*submit)(void *context, const char *command, uint32_t timeout_ms, ABoxBootV2AtDoneFn done, void *user);
    int (*send_payload)(void *context, const uint8_t *data, uint16_t length);
    int (*begin_raw_read)(void *context, uint32_t length);
    int (*has_pending)(void *context);
    int (*is_active)(void *context);
    void (*cancel)(void *context);
    /* Return success only when the event owner is registered. */
    int (*register_events)(void *context, ABoxBootV2AtEventFn callback, void *user);
    void (*mqtt_pause)(void *context, uint8_t paused);
    uint8_t (*mqtt_client_index)(void *context);
    void (*log)(void *context, uint8_t level, const char *message);
    uint8_t *transfer_buffer;
    uint32_t transfer_buffer_size;
    uint32_t (*rx_overflow_count)(void *context);
    /* Optional App-side transport ownership. Stop returns 1 only after
     * QMT close and drain, 0 while pending, -1 if modem reset is required.
     * Borrow/return brackets all OTA use of a shared transfer buffer. */
    int (*mqtt_stop)(void *context, uint32_t now_ms);
    int (*workspace_borrow)(void *context);
    void (*workspace_return)(void *context);
} ABoxBootV2AppPort;

typedef struct {
    const char *ota_origin;
    const char *revision_product;
    const char *artifact_name;
    const char *version_prefix;
    const char *running_version;
    const uint8_t *ca_pem;
    uint32_t ca_pem_length;
    uint32_t app_start_addr;
    uint32_t app_size;
    uint32_t app_max_size;
} ABoxBootV2AppConfig;

/* Startup/idle initialization only; callers must quiesce an active session
 * before rebinding. Every attempt invalidates readiness, including invalid
 * arguments. Until success, tasks/callbacks are inert and queries return safe
 * defaults (INIT_FAILED, ERROR_STATE, no metrics). */
int ABoxBootV2App_Init(const ABoxBootV2AppPort *port, const ABoxBootV2AppConfig *config);
int ABoxBootV2App_IsInitialized(void);
/* Called after physical modem reset (also handled by the RDY observer). */
void ABoxBootV2App_OnModemReset(void);
int ABoxBootV2App_BeginProvisioning(void);
int ABoxBootV2App_Start(const ABoxBootV2AppRequest *request);
void ABoxBootV2App_Task(void);
int ABoxBootV2App_IsProvisioned(void);
int ABoxBootV2App_IsReady(void);
int ABoxBootV2App_IsBusy(void);
const char *ABoxBootV2App_Phase(void);
const char *ABoxBootV2App_TargetVersion(void);
uint32_t ABoxBootV2App_LastError(void);
int ABoxBootV2App_TakeInstallReady(void);
int ABoxBootV2App_TakeFailure(uint32_t *error);
const ABoxHttpsUfsMetrics *ABoxBootV2App_DownloadMetrics(void);
const char *ABoxBootV2App_StableSeedSource(void);

#ifdef __cplusplus
}
#endif
#endif
