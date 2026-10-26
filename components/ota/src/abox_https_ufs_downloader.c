#include "abox_https_ufs_downloader.h"

#include <stdio.h>
#include <string.h>

enum {
    ST_IDLE = 0,
    ST_HTTP_STOP,
    ST_PDP_DEACT,
    ST_PDP_ACT,
    ST_TLS_VERSION,
    ST_TLS_LEVEL,
    ST_TLS_SNI,
    ST_TLS_CIPHER,
    ST_TLS_CA,
    ST_HTTP_CONTEXT,
    ST_HTTP_TLS,
    ST_REQUEST_HEADER,
    ST_RESPONSE_HEADER,
    ST_URL,
    ST_FILE_DELETE,
    ST_DIRECT_GET,
    ST_DIRECT_STORE,
    ST_RANGE_OPEN,
    ST_RANGE_GET,
    ST_RANGE_READ,
    ST_RANGE_WRITE,
    ST_RANGE_CLOSE,
    ST_FILE_LIST,
    ST_VERIFY_OPEN,
    ST_VERIFY_READ,
    ST_VERIFY_CLOSE,
    ST_VERIFY,
    ST_FAILURE_DELETE,
    ST_ERROR
};

static uint32_t now(const ABoxHttpsUfsDownloader *d)
{
    return d->port.tick_ms ? d->port.tick_ms(d->port.context) : 0U;
}

static void log_metrics(ABoxHttpsUfsDownloader *d, const char *result)
{
    char message[256];
    if (!d->port.log) return;
    (void)snprintf(message, sizeof(message),
                   "HTTPS/UFS %s phase=%s command=%s backend=%s fallback=%u get=%lums store=%lums verify=%lums total=%lums overflow=%lu error=%lu",
                   result,
                   d->failure_phase[0] ? d->failure_phase : ABoxHttpsUfs_Phase(d),
                   d->failure_command[0] ? d->failure_command : d->last_command,
                   d->metrics.backend == ABOX_HTTPS_UFS_BACKEND_DIRECT ? "direct" : "range",
                   (unsigned)d->metrics.fallback_reason,
                   (unsigned long)d->metrics.get_ms,
                   (unsigned long)d->metrics.ufs_store_ms,
                   (unsigned long)d->metrics.verify_ms,
                   (unsigned long)d->metrics.total_ms,
                   (unsigned long)d->metrics.rx_overflow,
                   (unsigned long)d->metrics.error);
    d->port.log(d->port.context, d->metrics.error ? 3U : 1U, message);
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    while (length--) {
        uint8_t bit;
        crc ^= *data++;
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

static int name_matches(const char *reported, const char *expected)
{
    if (strncmp(reported, "UFS:", 4U) == 0) reported += 4U;
    return strcmp(reported, expected) == 0;
}

static void command_done(ABoxHttpsUfsAtResult result, void *user);

static int submit(ABoxHttpsUfsDownloader *d, const char *command,
                  uint32_t timeout_ms)
{
    (void)snprintf(d->last_command, sizeof(d->last_command), "%s", command);
    d->payload_sent = 0U;
    if (d->port.submit && d->port.submit(d->port.context, command, timeout_ms,
                                         command_done, d))
        return 1;
    return 0;
}

static void finalize_failure(ABoxHttpsUfsDownloader *d, uint32_t error)
{
    d->metrics.error = error;
    d->metrics.total_ms = now(d) - d->started_at;
    if (d->port.rx_overflow_count)
        d->metrics.rx_overflow =
            d->port.rx_overflow_count(d->port.context) - d->rx_overflow_start;
    d->busy = 0U;
    d->failure_pending = 1U;
    d->state = ST_ERROR;
    log_metrics(d, "failed");
}

void ABoxHttpsUfs_OnModemReset(ABoxHttpsUfsDownloader *d)
{
    if (!d) return;
    if (d->port.cancel) d->port.cancel(d->port.context);
    d->handle_valid = d->payload_sent = 0U;
    if (d->busy) finalize_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
}

static void finish_failure(ABoxHttpsUfsDownloader *d, uint32_t error)
{
    char command[96];
    (void)snprintf(d->failure_command, sizeof(d->failure_command), "%s", d->last_command);
    (void)snprintf(d->failure_phase, sizeof(d->failure_phase), "%s", ABoxHttpsUfs_Phase(d));
    if (d->port.cancel) d->port.cancel(d->port.context);
    d->pending_error = error;
    d->state = ST_FAILURE_DELETE;
    (void)snprintf(command, sizeof(command), "AT+QFDEL=\"%s\"",
                   d->request.file);
    if (!submit(d, command, 5000U)) finalize_failure(d, error);
}

static void restart_session(ABoxHttpsUfsDownloader *d)
{
    d->downloaded = 0U;
    d->read_size = 0U;
    d->crc = 0xFFFFFFFFU;
    d->vector_length = 0U;
    d->handle_valid = 0U;
    d->file_seen = 0U;
    d->state = ST_HTTP_STOP;
    if (!submit(d, "AT+QHTTPSTOP", 10000U))
        finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
}

static void fallback_or_fail(ABoxHttpsUfsDownloader *d, uint32_t error,
                             ABoxHttpsUfsFallback reason)
{
    if (d->metrics.backend == ABOX_HTTPS_UFS_BACKEND_DIRECT) {
        if (reason == ABOX_HTTPS_UFS_FALLBACK_INTEGRITY) {
            d->metrics.backend = ABOX_HTTPS_UFS_BACKEND_RANGE;
            d->metrics.fallback_reason = reason;
            restart_session(d);
            return;
        }
        if (d->metrics.direct_attempts < 2U) {
            ++d->metrics.direct_attempts;
            d->recover_pdp = 1U;
            restart_session(d);
            return;
        }
        d->metrics.backend = ABOX_HTTPS_UFS_BACKEND_RANGE;
        d->metrics.fallback_reason = reason;
        /* A failed whole-response GET can leave the modem PDP/HTTP engine in
         * the same half-closed state that caused the direct retry to fail.
         * Rebuild the session before starting Range; QHTTPSTOP alone is not
         * sufficient on EC600E/EC800E after a truncated large response. */
        d->recover_pdp = 1U;
        restart_session(d);
        return;
    }
    finish_failure(d, error);
}

static void begin_verify(ABoxHttpsUfsDownloader *d)
{
    char command[96];
    d->file_seen = 0U;
    d->file_size = 0U;
    d->state = ST_FILE_LIST;
    (void)snprintf(command, sizeof(command), "AT+QFLST=\"%s\"",
                   d->request.file);
    if (!submit(d, command, 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
}

static void succeeded(ABoxHttpsUfsDownloader *d)
{
    char command[160];
    switch (d->state) {
    case ST_HTTP_STOP:
        if (d->recover_pdp) {
            d->recover_pdp = 0U;
            d->state = ST_PDP_DEACT;
            if (!submit(d, "AT+QIDEACT=1", 40000U))
                finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        } else {
            d->state = ST_TLS_VERSION;
            if (!submit(d, "AT+QSSLCFG=\"sslversion\",1,3", 5000U))
                finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        }
        break;
    case ST_PDP_DEACT:
        d->state = ST_PDP_ACT;
        if (!submit(d, "AT+QIACT=1", 150000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_PDP_ACT:
        d->state = ST_TLS_VERSION;
        if (!submit(d, "AT+QSSLCFG=\"sslversion\",1,3", 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_TLS_VERSION:
        d->state = ST_TLS_LEVEL;
        if (!submit(d, "AT+QSSLCFG=\"seclevel\",1,1", 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_TLS_LEVEL:
        d->state = ST_TLS_SNI;
        if (!submit(d, "AT+QSSLCFG=\"sni\",1,1", 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_TLS_SNI:
        /* Enrollment also uses context 1 and may leave an RSA-only suite.
         * Restore the modem's full TLS 1.2 suite set for the OTA endpoint. */
        d->state = ST_TLS_CIPHER;
        if (!submit(d, "AT+QSSLCFG=\"ciphersuite\",1,0xFFFF", 5000U))
            finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_TLS_CIPHER:
        d->state = ST_TLS_CA;
        (void)snprintf(command, sizeof(command), "AT+QSSLCFG=\"cacert\",1,\"%s\"",
                       d->config.ca_file);
        if (!submit(d, command, 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_TLS_CA:
        d->state = ST_HTTP_CONTEXT;
        if (!submit(d, "AT+QHTTPCFG=\"contextid\",1", 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_HTTP_CONTEXT:
        d->state = ST_HTTP_TLS;
        if (!submit(d, "AT+QHTTPCFG=\"sslctxid\",1", 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_HTTP_TLS:
        d->state = ST_REQUEST_HEADER;
        if (!submit(d, "AT+QHTTPCFG=\"requestheader\",0", 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_REQUEST_HEADER:
        d->state = ST_RESPONSE_HEADER;
        if (!submit(d, "AT+QHTTPCFG=\"responseheader\",0", 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_RESPONSE_HEADER:
        d->state = ST_URL;
        (void)snprintf(command, sizeof(command), "AT+QHTTPURL=%u,80",
                       (unsigned)strlen(d->request.url));
        if (!submit(d, command, 10000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_URL:
        d->state = ST_FILE_DELETE;
        (void)snprintf(command, sizeof(command), "AT+QFDEL=\"%s\"",
                       d->request.file);
        if (!submit(d, command, 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        break;
    case ST_FILE_DELETE:
        if (d->metrics.backend == ABOX_HTTPS_UFS_BACKEND_DIRECT) {
            d->state = ST_DIRECT_GET;
            d->http_status = 0U;
            d->http_length = 0U;
            d->get_started_at = now(d);
            if (!submit(d, "AT+QHTTPGET=80", 90000U)) fallback_or_fail(d, ABOX_HTTPS_UFS_ERROR_HTTP, ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR);
        } else {
            d->state = ST_RANGE_OPEN;
            (void)snprintf(command, sizeof(command), "AT+QFOPEN=\"%s\",1",
                           d->request.file);
            if (!submit(d, command, 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        }
        break;
    case ST_DIRECT_GET:
        d->metrics.get_ms += now(d) - d->get_started_at;
        if (d->http_status != 200U || d->http_length != d->request.size) {
            fallback_or_fail(d, ABOX_HTTPS_UFS_ERROR_HTTP, ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR);
            break;
        }
        d->state = ST_DIRECT_STORE;
        d->store_started_at = now(d);
        (void)snprintf(command, sizeof(command),
                       "AT+QHTTPREADFILE=\"UFS:%s\",80", d->request.file);
        if (!submit(d, command, 90000U)) fallback_or_fail(d, ABOX_HTTPS_UFS_ERROR_UFS, ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR);
        break;
    case ST_DIRECT_STORE:
        d->metrics.ufs_store_ms += now(d) - d->store_started_at;
        begin_verify(d);
        break;
    case ST_RANGE_OPEN:
        if (!d->handle_valid) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        else d->state = ST_RANGE_GET;
        break;
    case ST_RANGE_GET:
        d->metrics.get_ms += now(d) - d->get_started_at;
        if (d->http_status != 206U || d->http_length != d->chunk_size) {
            finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
            break;
        }
        d->chunk_received = 0U;
        d->state = ST_RANGE_READ;
        if (!submit(d, "AT+QHTTPREAD=80", 90000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
        break;
    case ST_RANGE_READ:
        if (d->chunk_received != d->chunk_size) {
            finish_failure(d, ABOX_HTTPS_UFS_ERROR_SIZE);
            break;
        }
        d->state = ST_RANGE_WRITE;
        d->store_started_at = now(d);
        (void)snprintf(command, sizeof(command), "AT+QFWRITE=%lu,%lu,10",
                       (unsigned long)d->handle, (unsigned long)d->chunk_size);
        if (!submit(d, command, 15000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        break;
    case ST_RANGE_WRITE:
        d->metrics.ufs_store_ms += now(d) - d->store_started_at;
        if (d->written != d->chunk_size ||
            d->written_total != d->downloaded + d->chunk_size) {
            finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
            break;
        }
        d->downloaded += d->chunk_size;
        if (d->downloaded == d->request.size) {
            d->state = ST_RANGE_CLOSE;
            (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu",
                           (unsigned long)d->handle);
            if (!submit(d, command, 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        } else d->state = ST_RANGE_GET;
        break;
    case ST_RANGE_CLOSE:
        d->handle_valid = 0U;
        begin_verify(d);
        break;
    case ST_FILE_LIST:
        if (!d->file_seen || d->file_size != d->request.size) {
            fallback_or_fail(d, ABOX_HTTPS_UFS_ERROR_SIZE, ABOX_HTTPS_UFS_FALLBACK_INTEGRITY);
            break;
        }
        d->state = ST_VERIFY_OPEN;
        (void)snprintf(command, sizeof(command), "AT+QFOPEN=\"%s\",2",
                       d->request.file);
        if (!submit(d, command, 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        break;
    case ST_VERIFY_OPEN:
        if (!d->handle_valid) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        else {
            d->read_size = 0U;
            d->crc = 0xFFFFFFFFU;
            d->vector_length = 0U;
            d->verify_started_at = now(d);
            d->state = ST_VERIFY_READ;
        }
        break;
    case ST_VERIFY_READ:
        if (d->read_size == d->request.size) {
            d->state = ST_VERIFY_CLOSE;
            (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu",
                           (unsigned long)d->handle);
            if (!submit(d, command, 5000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
        }
        break;
    case ST_VERIFY_CLOSE:
        d->handle_valid = 0U;
        d->state = ST_VERIFY;
        break;
    default:
        break;
    }
}

static void command_done(ABoxHttpsUfsAtResult result, void *user)
{
    ABoxHttpsUfsDownloader *d = (ABoxHttpsUfsDownloader *)user;
    if (!d || !d->busy) return;
    if (d->state == ST_FAILURE_DELETE) {
        finalize_failure(d, d->pending_error);
        return;
    }
    if (result == ABOX_HTTPS_UFS_AT_OK) {
        succeeded(d);
        return;
    }
    if ((d->state == ST_HTTP_STOP || d->state == ST_PDP_DEACT ||
         d->state == ST_FILE_DELETE) && result == ABOX_HTTPS_UFS_AT_ERROR) {
        succeeded(d);
        return;
    }
    if (d->metrics.backend == ABOX_HTTPS_UFS_BACKEND_DIRECT &&
        (d->state == ST_DIRECT_GET || d->state == ST_DIRECT_STORE))
        fallback_or_fail(d,
                         d->state == ST_DIRECT_GET ? ABOX_HTTPS_UFS_ERROR_HTTP
                                                   : ABOX_HTTPS_UFS_ERROR_UFS,
                         ABOX_HTTPS_UFS_FALLBACK_DIRECT_ERROR);
    else
        finish_failure(d, d->state <= ST_RESPONSE_HEADER ? ABOX_HTTPS_UFS_ERROR_HTTP
                                                          : ABOX_HTTPS_UFS_ERROR_UFS);
}

int ABoxHttpsUfs_Init(ABoxHttpsUfsDownloader *d,
                      const ABoxHttpsUfsPort *port,
                      const ABoxHttpsUfsConfig *config)
{
    if (!d || !port || !config || !port->tick_ms || !port->submit ||
        !port->send_payload || !port->begin_raw || !config->ca_file ||
        !config->transfer_buffer ||
        config->transfer_buffer_size < ABOX_HTTPS_UFS_RAW_CHUNK ||
        !config->vector_valid)
        return 0;
    memset(d, 0, sizeof(*d));
    d->port = *port;
    d->config = *config;
    return 1;
}

int ABoxHttpsUfs_Start(ABoxHttpsUfsDownloader *d,
                       const ABoxHttpsUfsRequest *request)
{
    if (!d || !request || d->busy || !request->size || !request->crc32 ||
        !request->url || !request->file || !request->url[0] ||
        !request->file[0] || strlen(request->url) >= ABOX_HTTPS_UFS_URL_SIZE ||
        strlen(request->file) >= ABOX_HTTPS_UFS_FILE_SIZE ||
        strncmp(request->url, "https://", 8U) != 0)
        return 0;
    d->request = *request;
    memset(&d->metrics, 0, sizeof(d->metrics));
    d->metrics.backend = d->config.direct_supported
                             ? ABOX_HTTPS_UFS_BACKEND_DIRECT
                             : ABOX_HTTPS_UFS_BACKEND_RANGE;
    d->metrics.fallback_reason = d->config.direct_supported
                                     ? ABOX_HTTPS_UFS_FALLBACK_NONE
                                     : ABOX_HTTPS_UFS_FALLBACK_UNSUPPORTED;
    d->metrics.direct_attempts = d->config.direct_supported ? 1U : 0U;
    d->busy = 1U;
    d->success_pending = 0U;
    d->failure_pending = 0U;
    d->started_at = now(d);
    d->rx_overflow_start = d->port.rx_overflow_count
                               ? d->port.rx_overflow_count(d->port.context)
                               : 0U;
    restart_session(d);
    return d->busy;
}

void ABoxHttpsUfs_SetDirectSupported(ABoxHttpsUfsDownloader *d, int supported)
{
    if (!d || d->busy) return;
    d->config.direct_supported = supported ? 1U : 0U;
}

void ABoxHttpsUfs_Task(ABoxHttpsUfsDownloader *d)
{
    char command[128];
    if (!d || !d->busy) return;
    if (d->port.is_active && d->port.is_active(d->port.context) &&
        !d->payload_sent) {
        if (d->state == ST_URL) {
            d->payload_sent = (uint8_t)d->port.send_payload(
                d->port.context, (const uint8_t *)d->request.url,
                (uint16_t)strlen(d->request.url));
            return;
        }
        if (d->state == ST_RANGE_WRITE) {
            d->payload_sent = (uint8_t)d->port.send_payload(
                d->port.context, d->config.transfer_buffer,
                (uint16_t)d->chunk_size);
            return;
        }
    }
    if (d->port.has_pending && d->port.has_pending(d->port.context)) return;
    if (d->state == ST_RANGE_GET) {
        d->chunk_size = d->request.size - d->downloaded;
        if (d->chunk_size > ABOX_HTTPS_UFS_RAW_CHUNK)
            d->chunk_size = ABOX_HTTPS_UFS_RAW_CHUNK;
        d->http_status = 0U;
        d->http_length = 0U;
        d->get_started_at = now(d);
        (void)snprintf(command, sizeof(command), "AT+QHTTPGETEX=80,%lu,%lu",
                       (unsigned long)d->downloaded,
                       (unsigned long)d->chunk_size);
        if (!submit(d, command, 90000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_HTTP);
    } else if (d->state == ST_VERIFY_READ) {
        uint32_t length = d->request.size - d->read_size;
        if (length > ABOX_HTTPS_UFS_RAW_CHUNK) length = ABOX_HTTPS_UFS_RAW_CHUNK;
        (void)snprintf(command, sizeof(command), "AT+QFREAD=%lu,%lu",
                       (unsigned long)d->handle, (unsigned long)length);
        if (!submit(d, command, 10000U)) finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
    } else if (d->state == ST_VERIFY) {
        uint32_t crc = d->crc ^ 0xFFFFFFFFU;
        d->metrics.verify_ms += now(d) - d->verify_started_at;
        if (d->read_size != d->request.size)
            fallback_or_fail(d, ABOX_HTTPS_UFS_ERROR_SIZE, ABOX_HTTPS_UFS_FALLBACK_INTEGRITY);
        else if (crc != d->request.crc32)
            fallback_or_fail(d, ABOX_HTTPS_UFS_ERROR_CRC, ABOX_HTTPS_UFS_FALLBACK_INTEGRITY);
        else if (!d->config.vector_valid(d->config.vector_context, d->vector))
            fallback_or_fail(d, ABOX_HTTPS_UFS_ERROR_VECTOR, ABOX_HTTPS_UFS_FALLBACK_INTEGRITY);
        else {
            d->metrics.error = 0U;
            d->metrics.total_ms = now(d) - d->started_at;
            if (d->port.rx_overflow_count)
                d->metrics.rx_overflow =
                    d->port.rx_overflow_count(d->port.context) -
                    d->rx_overflow_start;
            d->busy = 0U;
            d->success_pending = 1U;
            d->state = ST_IDLE;
            log_metrics(d, "verified");
        }
    }
}

void ABoxHttpsUfs_OnEvent(ABoxHttpsUfsDownloader *d,
                          ABoxHttpsUfsAtEvent event, const uint8_t *data,
                          uint16_t length)
{
    char line[160];
    if (!d || !d->busy || !data || !length) return;
    if (event == ABOX_HTTPS_UFS_AT_RAW) {
        if (d->state == ST_RANGE_READ) {
            if (d->chunk_received + length > d->chunk_size ||
                d->chunk_received + length > d->config.transfer_buffer_size) {
                finish_failure(d, ABOX_HTTPS_UFS_ERROR_SIZE);
                return;
            }
            memcpy(d->config.transfer_buffer + d->chunk_received, data, length);
            d->chunk_received += length;
        } else if (d->state == ST_VERIFY_READ) {
            uint16_t copy = length;
            if (d->read_size + length > d->request.size) {
                finish_failure(d, ABOX_HTTPS_UFS_ERROR_SIZE);
                return;
            }
            if (d->vector_length < sizeof(d->vector)) {
                if (copy > sizeof(d->vector) - d->vector_length)
                    copy = (uint16_t)(sizeof(d->vector) - d->vector_length);
                memcpy(d->vector + d->vector_length, data, copy);
                d->vector_length = (uint8_t)(d->vector_length + copy);
            }
            d->crc = crc_update(d->crc, data, length);
            d->read_size += length;
        }
        return;
    }
    if (length >= sizeof(line)) length = sizeof(line) - 1U;
    memcpy(line, data, length);
    line[length] = '\0';
    if (strncmp(line, "+QFOPEN:", 8U) == 0) {
        unsigned long handle;
        if (sscanf(line, "+QFOPEN: %lu", &handle) == 1) {
            d->handle = (uint32_t)handle;
            d->handle_valid = 1U;
        }
    } else if (strncmp(line, "+QHTTPGET:", 10U) == 0) {
        unsigned long result;
        unsigned long status;
        unsigned long response_length;
        if (sscanf(line, "+QHTTPGET: %lu,%lu,%lu", &result, &status,
                   &response_length) == 3 && result == 0U) {
            d->http_status = (uint32_t)status;
            d->http_length = (uint32_t)response_length;
        }
    } else if (strncmp(line, "+QFWRITE:", 9U) == 0) {
        unsigned long written;
        unsigned long total;
        if (sscanf(line, "+QFWRITE: %lu,%lu", &written, &total) == 2) {
            d->written = (uint32_t)written;
            d->written_total = (uint32_t)total;
        }
    } else if (strncmp(line, "+QFLST:", 7U) == 0) {
        char name[ABOX_HTTPS_UFS_FILE_SIZE + 4U];
        unsigned long size;
        if (sscanf(line, "+QFLST: \"%67[^\"]\",%lu", name, &size) == 2 &&
            name_matches(name, d->request.file)) {
            d->file_seen = 1U;
            d->file_size = (uint32_t)size;
        }
    }
    if (strcmp(line, "CONNECT") == 0 && d->state == ST_RANGE_READ) {
        if (!d->port.begin_raw(d->port.context, d->chunk_size))
            finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
    } else if (strncmp(line, "CONNECT ", 8U) == 0 &&
               d->state == ST_VERIFY_READ) {
        unsigned long raw_length;
        uint32_t remaining = d->request.size - d->read_size;
        if (sscanf(line, "CONNECT %lu", &raw_length) != 1 || !raw_length ||
            raw_length > ABOX_HTTPS_UFS_RAW_CHUNK || raw_length > remaining ||
            !d->port.begin_raw(d->port.context, (uint32_t)raw_length))
            finish_failure(d, ABOX_HTTPS_UFS_ERROR_UFS);
    }
}

int ABoxHttpsUfs_IsBusy(const ABoxHttpsUfsDownloader *d)
{
    return d && d->busy;
}

const char *ABoxHttpsUfs_Phase(const ABoxHttpsUfsDownloader *d)
{
    if (!d) return "ERROR";
    if (d->state >= ST_VERIFY_OPEN && d->state <= ST_VERIFY) return "VERIFYING";
    if (d->state == ST_ERROR) return "ERROR";
    if (d->state == ST_IDLE) return "IDLE";
    return d->metrics.backend == ABOX_HTTPS_UFS_BACKEND_DIRECT
               ? "DOWNLOADING_DIRECT"
               : "DOWNLOADING_RANGE";
}

int ABoxHttpsUfs_TakeSuccess(ABoxHttpsUfsDownloader *d)
{
    int value;
    if (!d) return 0;
    value = d->success_pending;
    d->success_pending = 0U;
    return value;
}

int ABoxHttpsUfs_TakeFailure(ABoxHttpsUfsDownloader *d, uint32_t *error)
{
    int value;
    if (!d) return 0;
    value = d->failure_pending;
    d->failure_pending = 0U;
    if (error) *error = d->metrics.error;
    return value;
}

const ABoxHttpsUfsMetrics *ABoxHttpsUfs_Metrics(const ABoxHttpsUfsDownloader *d)
{
    return d ? &d->metrics : 0;
}
