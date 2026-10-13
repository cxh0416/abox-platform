#ifndef ABOX_HTTPS_UFS_DOWNLOADER_H
#define ABOX_HTTPS_UFS_DOWNLOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABOX_HTTPS_UFS_URL_SIZE 256U
#define ABOX_HTTPS_UFS_FILE_SIZE 64U
#define ABOX_HTTPS_UFS_RAW_CHUNK 1024U

typedef enum {
    ABOX_HTTPS_UFS_AT_OK = 0,
    ABOX_HTTPS_UFS_AT_ERROR,
    ABOX_HTTPS_UFS_AT_TIMEOUT,
    ABOX_HTTPS_UFS_AT_SEND_OK,
    ABOX_HTTPS_UFS_AT_SEND_FAIL
} ABoxHttpsUfsAtResult;

typedef enum {
    ABOX_HTTPS_UFS_AT_LINE = 0,
    ABOX_HTTPS_UFS_AT_RAW
} ABoxHttpsUfsAtEvent;

typedef enum {
    ABOX_HTTPS_UFS_BACKEND_DIRECT = 1,
    ABOX_HTTPS_UFS_BACKEND_RANGE = 2
} ABoxHttpsUfsBackend;

typedef enum {
    ABOX_HTTPS_UFS_FALLBACK_NONE = 0,
    ABOX_HTTPS_UFS_FALLBACK_UNSUPPORTED,
    ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR,
    ABOX_HTTPS_UFS_FALLBACK_INTEGRITY
} ABoxHttpsUfsFallback;

#define ABOX_HTTPS_UFS_ERROR_HTTP 1101U
#define ABOX_HTTPS_UFS_ERROR_UFS 1201U
#define ABOX_HTTPS_UFS_ERROR_SIZE 1301U
#define ABOX_HTTPS_UFS_ERROR_CRC 1302U
#define ABOX_HTTPS_UFS_ERROR_VECTOR 1303U
#define ABOX_HTTPS_UFS_ERROR_CONFIG 1402U

typedef void (*ABoxHttpsUfsDoneFn)(ABoxHttpsUfsAtResult result, void *user);

typedef struct {
    void *context;
    uint32_t (*tick_ms)(void *context);
    int (*submit)(void *context, const char *command, uint32_t timeout_ms,
                  ABoxHttpsUfsDoneFn done, void *user);
    int (*send_payload)(void *context, const uint8_t *data, uint16_t length);
    int (*begin_raw)(void *context, uint32_t length);
    int (*has_pending)(void *context);
    int (*is_active)(void *context);
    void (*cancel)(void *context);
    void (*log)(void *context, uint8_t level, const char *message);
    uint32_t (*rx_overflow_count)(void *context);
} ABoxHttpsUfsPort;

typedef int (*ABoxHttpsUfsVectorFn)(void *context, const uint8_t vector[8]);

typedef struct {
    const char *ca_file;
    uint8_t *transfer_buffer;
    uint32_t transfer_buffer_size;
    ABoxHttpsUfsVectorFn vector_valid;
    void *vector_context;
    uint8_t direct_supported;
} ABoxHttpsUfsConfig;

typedef struct {
    /* These immutable strings must remain valid until the transfer finishes. */
    const char *url;
    const char *file;
    uint32_t size;
    uint32_t crc32;
} ABoxHttpsUfsRequest;

typedef struct {
    ABoxHttpsUfsBackend backend;
    ABoxHttpsUfsFallback fallback_reason;
    uint32_t get_ms;
    uint32_t ufs_store_ms;
    uint32_t verify_ms;
    uint32_t total_ms;
    uint32_t rx_overflow;
    uint32_t error;
    uint8_t direct_attempts;
} ABoxHttpsUfsMetrics;

typedef struct ABoxHttpsUfsDownloader ABoxHttpsUfsDownloader;

int ABoxHttpsUfs_Init(ABoxHttpsUfsDownloader *downloader,
                      const ABoxHttpsUfsPort *port,
                      const ABoxHttpsUfsConfig *config);
int ABoxHttpsUfs_Start(ABoxHttpsUfsDownloader *downloader,
                       const ABoxHttpsUfsRequest *request);
void ABoxHttpsUfs_SetDirectSupported(ABoxHttpsUfsDownloader *downloader,
                                     int supported);
void ABoxHttpsUfs_Task(ABoxHttpsUfsDownloader *downloader);
/* Physical modem reset invalidates HTTP/UFS handles; settle without issuing
 * cleanup commands against handles from the previous modem session. */
void ABoxHttpsUfs_OnModemReset(ABoxHttpsUfsDownloader *downloader);
void ABoxHttpsUfs_OnEvent(ABoxHttpsUfsDownloader *downloader,
                          ABoxHttpsUfsAtEvent event, const uint8_t *data,
                          uint16_t length);
int ABoxHttpsUfs_IsBusy(const ABoxHttpsUfsDownloader *downloader);
const char *ABoxHttpsUfs_Phase(const ABoxHttpsUfsDownloader *downloader);
int ABoxHttpsUfs_TakeSuccess(ABoxHttpsUfsDownloader *downloader);
int ABoxHttpsUfs_TakeFailure(ABoxHttpsUfsDownloader *downloader,
                             uint32_t *error);
const ABoxHttpsUfsMetrics *ABoxHttpsUfs_Metrics(
    const ABoxHttpsUfsDownloader *downloader);

/* Static allocation is intentional: firmware targets do not use malloc. */
struct ABoxHttpsUfsDownloader {
    ABoxHttpsUfsPort port;
    ABoxHttpsUfsConfig config;
    ABoxHttpsUfsRequest request;
    ABoxHttpsUfsMetrics metrics;
    uint32_t state;
    uint32_t handle;
    uint32_t downloaded;
    uint32_t chunk_size;
    uint32_t chunk_received;
    uint32_t read_size;
    uint32_t crc;
    uint32_t http_status;
    uint32_t http_length;
    uint32_t written;
    uint32_t written_total;
    uint32_t file_size;
    uint32_t started_at;
    uint32_t get_started_at;
    uint32_t store_started_at;
    uint32_t verify_started_at;
    uint32_t rx_overflow_start;
    uint32_t pending_error;
    char last_command[80];
    char failure_command[80];
    char failure_phase[24];
    uint8_t vector[8];
    uint8_t vector_length;
    uint8_t handle_valid;
    uint8_t file_seen;
    uint8_t payload_sent;
    uint8_t busy;
    uint8_t success_pending;
    uint8_t failure_pending;
    uint8_t recover_pdp;
};

#ifdef __cplusplus
}
#endif
#endif
