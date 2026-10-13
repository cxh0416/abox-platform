#include "abox_enrollment_ec800_http.h"

#include <stdio.h>
#include <string.h>

static void command_done(ABoxEc800Result result, void *user);

static int submit(ABoxEnrollmentEc800Http *h, const char *command, uint32_t timeout)
{
    return ABoxEc800At_Submit(h->at, command, h->owner, ABOX_EC800_PRIORITY_HIGH,
                             timeout, command_done, h);
}

static int ca_submit(ABoxEnrollmentEc800Http *h, ABoxEnrollmentHttpState state,
                     const char *format, uint32_t value)
{
    h->state = state;
    if (state == ABOX_ENROLL_CA_READ)
        snprintf(h->command, sizeof(h->command), format, (unsigned long)h->ca_handle, (unsigned long)value);
    else if (state == ABOX_ENROLL_CA_CLOSE)
        snprintf(h->command, sizeof(h->command), format, (unsigned long)h->ca_handle);
    else if (state == ABOX_ENROLL_CA_UPLOAD)
        snprintf(h->command, sizeof(h->command), format, h->ca_file, (unsigned long)h->ca_length);
    else snprintf(h->command, sizeof(h->command), format, h->ca_file);
    if (submit(h, h->command, 30000U)) return 1;
    h->blocked = 1U;
    h->state = ABOX_ENROLL_HTTP_IDLE;
    return 0;
}

static void ca_done(ABoxEnrollmentEc800Http *h, ABoxEc800Result r)
{
    if (r != ABOX_EC800_RESULT_OK && h->state != ABOX_ENROLL_CA_LIST &&
        h->state != ABOX_ENROLL_CA_DELETE) {
        h->blocked = 1U;
        h->state = ABOX_ENROLL_HTTP_IDLE;
        /* A timeout with an open file/raw owner requires physical recovery. */
        if (h->ca_handle_valid) ABoxEc800At_Quarantine(h->at);
        return;
    }
    switch (h->state) {
    case ABOX_ENROLL_CA_LIST:
        if (r == ABOX_EC800_RESULT_OK && h->ca_match)
            ca_submit(h, ABOX_ENROLL_CA_OPEN, "AT+QFOPEN=\"%s\",2", 0);
        else if (h->ca_uploaded) {
            h->blocked = 1U; h->state = ABOX_ENROLL_HTTP_IDLE;
        }
        else ca_submit(h, ABOX_ENROLL_CA_DELETE, "AT+QFDEL=\"%s\"", 0);
        break;
    case ABOX_ENROLL_CA_OPEN:
        if (!h->ca_handle_valid) { h->blocked = 1U; h->state = ABOX_ENROLL_HTTP_IDLE; break; }
        h->ca_offset = 0; h->ca_match = 1;
        ca_submit(h, ABOX_ENROLL_CA_READ, "AT+QFREAD=%lu,%lu",
                  h->ca_length > 1024U ? 1024U : h->ca_length);
        break;
    case ABOX_ENROLL_CA_READ:
        if (h->ca_offset < h->ca_length && h->ca_match) {
            uint32_t n = h->ca_length - h->ca_offset;
            ca_submit(h, ABOX_ENROLL_CA_READ, "AT+QFREAD=%lu,%lu", n > 1024U ? 1024U : n);
        } else ca_submit(h, ABOX_ENROLL_CA_CLOSE, "AT+QFCLOSE=%lu", 0);
        break;
    case ABOX_ENROLL_CA_CLOSE:
        h->ca_handle_valid = 0;
        if (h->ca_match && h->ca_offset == h->ca_length) {
            h->ca_verified = 1;
            h->state = ABOX_ENROLL_HTTP_IDLE;
        } else if (!h->ca_uploaded)
            ca_submit(h, ABOX_ENROLL_CA_DELETE, "AT+QFDEL=\"%s\"", 0);
        else { h->blocked = 1U; h->state = ABOX_ENROLL_HTTP_IDLE; }
        break;
    case ABOX_ENROLL_CA_DELETE:
        ca_submit(h, ABOX_ENROLL_CA_UPLOAD, "AT+QFUPL=\"%s\",%lu,30", 0);
        break;
    case ABOX_ENROLL_CA_UPLOAD:
        h->ca_uploaded = 1; h->ca_match = 0;
        ca_submit(h, ABOX_ENROLL_CA_LIST, "AT+QFLST=\"%s\"", 0);
        break;
    default: break;
    }
}

static void finish(ABoxEnrollmentEc800Http *h, int clean)
{
    ABoxEnrollmentHttpDone done = h->done;
    void *user = h->user;
    uint16_t status = h->status;
    size_t length = h->response_length;
    if (!clean) h->blocked = 1U;
    h->state = ABOX_ENROLL_HTTP_IDLE;
    h->url = NULL; h->body = NULL; h->response = NULL;
    if (done) done(user, status, length, clean && !h->overflow && !h->cancelling);
}

static void stop_http(ABoxEnrollmentEc800Http *h)
{
    /* PDP/TLS configuration has not opened an HTTP session. QHTTPSTOP may
     * return ERROR here; that is not evidence of an unclosed HTTP lease. */
    if ((h->state >= ABOX_ENROLL_HTTP_TLS_VERSION &&
         h->state <= ABOX_ENROLL_HTTP_RESP_HEADER) ||
        h->state == ABOX_ENROLL_HTTP_PDP_QUERY ||
        h->state == ABOX_ENROLL_HTTP_PDP_ACTIVATE) {
        finish(h, 1);
        return;
    }
    h->state = ABOX_ENROLL_HTTP_STOP;
    if (!submit(h, "AT+QHTTPSTOP", 10000U)) finish(h, 0);
}

static void command_done(ABoxEc800Result result, void *user)
{
    ABoxEnrollmentEc800Http *h = user;
    int count;
    if (!h || h->state == ABOX_ENROLL_HTTP_IDLE) return;
    if (h->state >= ABOX_ENROLL_CA_LIST) { ca_done(h, result); return; }
    if (h->state == ABOX_ENROLL_HTTP_STOP) {
        finish(h, result == ABOX_EC800_RESULT_OK);
        return;
    }
    if (result != ABOX_EC800_RESULT_OK || h->cancelling) {
        stop_http(h);
        return;
    }
    switch (h->state) {
    case ABOX_ENROLL_HTTP_PDP_QUERY:
        if (!h->pdp_active) {
            h->state = ABOX_ENROLL_HTTP_PDP_ACTIVATE;
            if (!submit(h, "AT+QIACT=1", 150000U)) stop_http(h);
            break;
        }
        /* fall through */
    case ABOX_ENROLL_HTTP_PDP_ACTIVATE:
        h->state = ABOX_ENROLL_HTTP_TLS_VERSION;
        if (!submit(h, "AT+QSSLCFG=\"sslversion\",1,3", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_VERSION:
        h->state = ABOX_ENROLL_HTTP_TLS_LEVEL;
        if (!submit(h, "AT+QSSLCFG=\"seclevel\",1,1", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_LEVEL:
        h->state = ABOX_ENROLL_HTTP_TLS_SNI;
        if (!submit(h, "AT+QSSLCFG=\"sni\",1,1", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_SNI:
        h->state = ABOX_ENROLL_HTTP_TLS_TIME;
        if (!submit(h, "AT+QSSLCFG=\"ignorelocaltime\",1,0", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_TIME:
        if (!h->ca_pem) {
            h->state = ABOX_ENROLL_HTTP_TLS_CIPHER;
            command_done(ABOX_EC800_RESULT_OK, h);
            break;
        }
        h->state = ABOX_ENROLL_HTTP_TLS_ITEMS;
        if (!submit(h, "AT+QSSLCFG=\"ignorecertitem\",1,0", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_ITEMS:
        h->state = ABOX_ENROLL_HTTP_TLS_SIGNATURE;
        if (!submit(h, "AT+QSSLCFG=\"ignoreinvalidcertsign\",1,0", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_SIGNATURE:
        h->state = ABOX_ENROLL_HTTP_TLS_CHAIN;
        if (!submit(h, "AT+QSSLCFG=\"ignoremulticertchainverify\",1,0", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_CHAIN:
        h->state = ABOX_ENROLL_HTTP_TLS_CIPHER;
        if (!submit(h, "AT+QSSLCFG=\"ciphersuite\",1,0x009C", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_CIPHER:
        count = snprintf(h->command, sizeof(h->command),
                         "AT+QSSLCFG=\"cacert\",1,\"%s\"", h->ca_file);
        if (count < 0 || (size_t)count >= sizeof(h->command)) { stop_http(h); break; }
        h->state = ABOX_ENROLL_HTTP_TLS_CA;
        if (!submit(h, h->command, 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_TLS_CA:
        h->state = ABOX_ENROLL_HTTP_CONTEXT;
        if (!submit(h, "AT+QHTTPCFG=\"contextid\",1", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_CONTEXT:
        h->state = ABOX_ENROLL_HTTP_SSL_CONTEXT;
        if (!submit(h, "AT+QHTTPCFG=\"sslctxid\",1", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_SSL_CONTEXT:
        h->state = ABOX_ENROLL_HTTP_REQ_HEADER;
        if (!submit(h, "AT+QHTTPCFG=\"requestheader\",0", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_REQ_HEADER:
        h->state = ABOX_ENROLL_HTTP_RESP_HEADER;
        if (!submit(h, "AT+QHTTPCFG=\"responseheader\",0", 5000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_RESP_HEADER:
        count = snprintf(h->command, sizeof(h->command), "AT+QHTTPURL=%u,80",
                         (unsigned)strlen(h->url));
        if (count < 0 || (size_t)count >= sizeof(h->command)) { stop_http(h); break; }
        h->state = ABOX_ENROLL_HTTP_URL;
        if (!submit(h, h->command, 10000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_URL:
        if (!h->url_sent) { stop_http(h); break; }
        count = snprintf(h->command, sizeof(h->command), "AT+QHTTPPOST=%u,80,80",
                         (unsigned)strlen(h->body));
        if (count < 0 || (size_t)count >= sizeof(h->command)) { stop_http(h); break; }
        h->state = ABOX_ENROLL_HTTP_POST;
        if (!submit(h, h->command, 90000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_POST:
        if (!h->body_sent || !h->status || !h->expected_length ||
            h->expected_length >= h->response_capacity) { stop_http(h); break; }
        h->state = ABOX_ENROLL_HTTP_READ;
        if (!submit(h, "AT+QHTTPREAD=80", 90000U)) stop_http(h);
        break;
    case ABOX_ENROLL_HTTP_READ:
        if (h->response_length != h->expected_length) h->overflow = 1U;
        /* QHTTPREAD's successful final URC completes and closes the session.
         * QHTTPSTOP cancels an active request; some modem firmware rejects it
         * after a completed read, which must not discard a valid response. */
        finish(h, 1);
        break;
    default:
        stop_http(h);
        break;
    }
}

static void on_event(ABoxEc800Event event, const uint8_t *data,
                     uint16_t length, void *user)
{
    ABoxEnrollmentEc800Http *h = user;
    char line[96];
    unsigned long result, status, size;
    if (!h || !data) return;
    if (event == ABOX_EC800_EVENT_LINE && length == 3U && !memcmp(data, "RDY", 3U)) {
        ABoxEnrollmentEc800Http_AfterModemReset(h);
        return;
    }
    if (h->state == ABOX_ENROLL_HTTP_IDLE) return;
    if (event == ABOX_EC800_EVENT_RAW) {
        if (h->state == ABOX_ENROLL_CA_READ) {
            if (length > h->ca_length - h->ca_offset ||
                memcmp(data, h->ca_pem + h->ca_offset, length)) h->ca_match = 0;
            if (length <= h->ca_length - h->ca_offset) h->ca_offset += length;
            return;
        }
        if (h->state != ABOX_ENROLL_HTTP_READ ||
            length > h->response_capacity - 1U - h->response_length) {
            h->overflow = 1U;
            return;
        }
        memcpy(h->response + h->response_length, data, length);
        h->response_length += length;
        h->response[h->response_length] = '\0';
        return;
    }
    if (length >= sizeof(line)) return;
    memcpy(line, data, length);
    line[length] = '\0';
    if (h->state >= ABOX_ENROLL_CA_LIST) {
        char name[64]; unsigned long n;
        if (h->state == ABOX_ENROLL_CA_LIST &&
            sscanf(line, "+QFLST: \"%63[^\"]\",%lu", name, &n) == 2) {
            const char *expected = h->ca_file + (strncmp(h->ca_file, "UFS:", 4) ? 0 : 4);
            const char *actual = name + (strncmp(name, "UFS:", 4) ? 0 : 4);
            h->ca_match = (uint8_t)(!strcmp(actual, expected) && n == h->ca_length);
        } else if (h->state == ABOX_ENROLL_CA_OPEN && sscanf(line, "+QFOPEN: %lu", &n) == 1) {
            h->ca_handle = (uint32_t)n; h->ca_handle_valid = 1;
        } else if (h->state == ABOX_ENROLL_CA_READ && sscanf(line, "CONNECT %lu", &n) == 1) {
            if (!n || n > 1024U || n > h->ca_length - h->ca_offset ||
                !ABoxEc800At_BeginRaw(h->at, h->owner, (uint32_t)n)) {
                h->ca_match = 0; h->blocked = 1U;
                ABoxEc800At_Quarantine(h->at);
            }
        } else if (h->state == ABOX_ENROLL_CA_UPLOAD && !strcmp(line, "CONNECT")) {
            if (!ABoxEc800At_SendPayload(h->at, h->owner, h->ca_pem, (uint16_t)h->ca_length))
                h->blocked = 1U;
        }
        return;
    }
    if (h->state == ABOX_ENROLL_HTTP_PDP_QUERY) {
        unsigned context_id, active;
        if (sscanf(line, "+QIACT: %u,%u", &context_id, &active) == 2 && context_id == 1U)
            h->pdp_active = (uint8_t)(active == 1U);
    }
    if (!strcmp(line, "CONNECT") && h->state == ABOX_ENROLL_HTTP_URL && !h->url_sent) {
        h->url_sent = ABoxEc800At_SendPayload(h->at, h->owner,
                                              (const uint8_t *)h->url, (uint16_t)strlen(h->url));
    } else if (!strcmp(line, "CONNECT") && h->state == ABOX_ENROLL_HTTP_POST && !h->body_sent) {
        h->body_sent = ABoxEc800At_SendPayload(h->at, h->owner,
                                               (const uint8_t *)h->body, (uint16_t)strlen(h->body));
    } else if (!strcmp(line, "CONNECT") && h->state == ABOX_ENROLL_HTTP_READ) {
        if (!ABoxEc800At_BeginRaw(h->at, h->owner, h->expected_length)) h->overflow = 1U;
    } else if (h->state == ABOX_ENROLL_HTTP_POST &&
               sscanf(line, "+QHTTPPOST: %lu,%lu,%lu", &result, &status, &size) == 3 &&
               result == 0UL && status <= 65535UL) {
        h->status = (uint16_t)status;
        h->expected_length = (uint32_t)size;
    }
}

int ABoxEnrollmentEc800Http_Init(ABoxEnrollmentEc800Http *h, ABoxEc800At *at,
                                 ABoxEc800Owner owner, const char *ca_file,
                                 ABoxEnrollmentHttpDone done, void *user)
{
    if (!h || !at || owner == ABOX_EC800_OWNER_NONE || !ca_file || !ca_file[0] ||
        strlen(ca_file) > 80U || !done) return 0;
    memset(h, 0, sizeof(*h));
    h->at = at; h->owner = owner; h->ca_file = ca_file; h->done = done; h->user = user;
    if (!ABoxEc800At_Register(at, owner, on_event, h)) {
        h->at = NULL;
        return 0;
    }
    return 1;
}

int ABoxEnrollmentEc800Http_Ready(const ABoxEnrollmentEc800Http *h)
{ return h && h->at && !h->blocked && (!h->ca_pem || h->ca_verified) &&
         h->state == ABOX_ENROLL_HTTP_IDLE && !ABoxEc800At_IsBusy(h->at); }

int ABoxEnrollmentEc800Http_SetCaPem(ABoxEnrollmentEc800Http *h, const uint8_t *pem, uint32_t n)
{
    if (!h || !h->at || h->state != ABOX_ENROLL_HTTP_IDLE || !pem || !n || n > UINT16_MAX ||
        !strcmp(h->ca_file, "UFS:ota_ca.pem") || !strcmp(h->ca_file, "ota_ca.pem")) return 0;
    h->ca_pem = pem; h->ca_length = n; h->ca_verified = 0;
    return 1;
}

void ABoxEnrollmentEc800Http_PrepareCa(ABoxEnrollmentEc800Http *h)
{
    if (!h || !h->ca_pem || h->ca_verified || h->blocked ||
        h->state != ABOX_ENROLL_HTTP_IDLE || ABoxEc800At_IsBusy(h->at)) return;
    h->ca_match = 0; h->ca_uploaded = 0; h->ca_handle_valid = 0;
    ca_submit(h, ABOX_ENROLL_CA_LIST, "AT+QFLST=\"%s\"", 0);
}

int ABoxEnrollmentEc800Http_Post(ABoxEnrollmentEc800Http *h,
                                 const char *url, const char *body,
                                 char *response, size_t capacity)
{
    size_t url_length, body_length;
    if (!ABoxEnrollmentEc800Http_Ready(h) || !url || !body || !response || capacity < 2U)
        return 0;
    url_length = strlen(url); body_length = strlen(body);
    if (!url_length || url_length > UINT16_MAX || !body_length || body_length > UINT16_MAX ||
        strncmp(url, "https://", 8U)) return 0;
    h->url = url; h->body = body; h->response = response;
    h->response_capacity = capacity; h->response_length = 0U;
    h->expected_length = 0U; h->status = 0U;
    h->url_sent = 0U; h->body_sent = 0U;
    h->overflow = 0U; h->cancelling = 0U;
    response[0] = '\0';
    h->pdp_active = 0U;
    h->state = ABOX_ENROLL_HTTP_PDP_QUERY;
    if (!submit(h, "AT+QIACT?", 5000U)) {
        h->state = ABOX_ENROLL_HTTP_IDLE;
        return 0;
    }
    return 1;
}

void ABoxEnrollmentEc800Http_Cancel(ABoxEnrollmentEc800Http *h)
{ if (h && h->state != ABOX_ENROLL_HTTP_IDLE) h->cancelling = 1U; }

void ABoxEnrollmentEc800Http_AfterModemReset(ABoxEnrollmentEc800Http *h)
{
    if (!h) return;
    ABoxEc800At_Cancel(h->at, h->owner);
    if (h->state != ABOX_ENROLL_HTTP_IDLE && h->state < ABOX_ENROLL_CA_LIST) {
        h->status = 0U;
        h->response_length = 0U;
        finish(h, 1); /* Reset proves the previous HTTP session is closed. */
    }
    h->state = ABOX_ENROLL_HTTP_IDLE;
    h->blocked = 0U;
    h->cancelling = 0U;
    h->url = NULL; h->body = NULL; h->response = NULL;
    h->ca_verified = 0; h->ca_handle_valid = 0; h->ca_uploaded = 0;
}
