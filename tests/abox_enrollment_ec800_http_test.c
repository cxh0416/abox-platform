#include "abox_enrollment_ec800_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static uint32_t tick(void *context) { (void)context; return 0U; }
static char sent[2048];
static int fail_write;
static int write_bytes(void *context, const uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)context; (void)timeout;
    if (fail_write) return 0;
    CHECK(length < sizeof(sent));
    memcpy(sent, data, length); sent[length] = '\0';
    return 1;
}
typedef struct { unsigned calls; uint16_t status; size_t length; int clean; } Result;
static void done(void *user, uint16_t status, size_t length, int clean)
{
    Result *r = user;
    ++r->calls; r->status = status; r->length = length; r->clean = clean;
}
static void feed(ABoxEc800At *at, const char *line)
{ ABoxEc800At_Feed(at, (const uint8_t *)line, (uint16_t)strlen(line)); }

int main(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort at_port = {0, tick, write_bytes, 0, 0};
    ABoxEnrollmentEc800Http http;
    Result result = {0};
    char response[64];
    unsigned i;
    CHECK(ABoxEc800At_Init(&at, &at_port));
    CHECK(ABoxEnrollmentEc800Http_Init(&http, &at, ABOX_EC800_OWNER_PRODUCT_BASE + 2U,
                                       "UFS:ota_ca.pem", done, &result));
    CHECK(ABoxEnrollmentEc800Http_Post(&http, "https://ota.example/requests", "{}",
                                       response, sizeof(response)));
    for (i = 0U; i < 11U; ++i) {
        ABoxEc800At_Task(&at);
        feed(&at, "OK\r\n");
    }
    ABoxEc800At_Task(&at);
    CHECK(strstr(sent, "QHTTPURL") != NULL);
    feed(&at, "CONNECT\r\n");
    feed(&at, "OK\r\n");
    ABoxEc800At_Task(&at);
    CHECK(strstr(sent, "QHTTPPOST") != NULL);
    feed(&at, "CONNECT\r\n");
    feed(&at, "OK\r\n+QHTTPPOST: 0,201,2\r\n");
    ABoxEc800At_Task(&at);
    CHECK(strstr(sent, "QHTTPREAD") != NULL);
    feed(&at, "CONNECT\r\n");
    feed(&at, "{}\r\nOK\r\n+QHTTPREAD: 0\r\n");
    CHECK(result.calls == 1U && result.status == 201U && result.length == 2U && result.clean);
    CHECK(!strcmp(response, "{}"));
    CHECK(ABoxEnrollmentEc800Http_Ready(&http));
    CHECK(ABoxEnrollmentEc800Http_Post(&http, "https://ota.example/poll", "{}",
                                       response, sizeof(response)));
    ABoxEc800At_Task(&at);
    CHECK(strstr(sent, "QIACT?") != NULL);
    feed(&at, "ERROR\r\n");
    CHECK(result.calls == 2U && result.status == 0U && result.clean);
    CHECK(ABoxEnrollmentEc800Http_Ready(&http));
    CHECK(ABoxEnrollmentEc800Http_Post(&http, "https://ota.example/poll", "{}",
                                       response, sizeof(response)));
    for (i = 0U; i < 11U; ++i) {
        ABoxEc800At_Task(&at);
        feed(&at, "OK\r\n");
    }
    ABoxEc800At_Task(&at);
    CHECK(strstr(sent, "QHTTPURL") != NULL);
    feed(&at, "ERROR\r\n");
    ABoxEc800At_Task(&at);
    CHECK(strstr(sent, "QHTTPSTOP") != NULL);
    feed(&at, "ERROR\r\n");
    CHECK(result.calls == 3U && result.status == 0U && !result.clean);
    CHECK(!ABoxEnrollmentEc800Http_Ready(&http));
    ABoxEnrollmentEc800Http_AfterModemReset(&http);
    CHECK(ABoxEnrollmentEc800Http_Ready(&http));
    uint8_t pem[1200]; memset(pem, 'A', sizeof(pem));
    CHECK(!ABoxEnrollmentEc800Http_SetCaPem(&http, pem, sizeof(pem)));
    CHECK(ABoxEc800At_Init(&at, &at_port));
    CHECK(ABoxEnrollmentEc800Http_Init(&http, &at, ABOX_EC800_OWNER_PRODUCT_BASE + 2U,
                                       "UFS:enrollment_ca_v1.pem", done, &result));
    CHECK(ABoxEnrollmentEc800Http_SetCaPem(&http, pem, sizeof(pem)));
    CHECK(!ABoxEnrollmentEc800Http_Ready(&http));
    ABoxEnrollmentEc800Http_PrepareCa(&http);
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFLST")); feed(&at, "ERROR\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFDEL")); feed(&at, "ERROR\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFUPL")); feed(&at, "CONNECT\r\n");
    CHECK(strlen(sent) == sizeof(pem)); feed(&at, "+QFUPL: 1200,0\r\nOK\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFLST"));
    feed(&at, "+QFLST: \"enrollment_ca_v1.pem\",1200\r\nOK\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFOPEN")); feed(&at, "+QFOPEN: 1\r\nOK\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFREAD=1,1024")); feed(&at, "CONNECT 1024\r\n");
    ABoxEc800At_Feed(&at, pem, 1024); feed(&at, "\r\nOK\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFREAD=1,176")); feed(&at, "CONNECT 176\r\n");
    ABoxEc800At_Feed(&at, pem + 1024, 176); feed(&at, "\r\nOK\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFCLOSE=1")); feed(&at, "OK\r\n");
    CHECK(ABoxEnrollmentEc800Http_Ready(&http));
    ABoxEnrollmentEc800Http_AfterModemReset(&http);
    CHECK(!ABoxEnrollmentEc800Http_Ready(&http));
    ABoxEnrollmentEc800Http_PrepareCa(&http);
    ABoxEc800At_Task(&at); feed(&at, "+QFLST: \"enrollment_ca_v1.pem\",1200\r\nOK\r\n");
    ABoxEc800At_Task(&at); feed(&at, "+QFOPEN: 2\r\nOK\r\n");
    ABoxEc800At_Task(&at); feed(&at, "CONNECT 1024\r\n");
    uint8_t corrupt[1024]; memcpy(corrupt, pem, sizeof(corrupt)); corrupt[0] = 'B';
    ABoxEc800At_Feed(&at, corrupt, sizeof(corrupt)); feed(&at, "\r\nOK\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFCLOSE=2")); feed(&at, "OK\r\n");
    ABoxEc800At_Task(&at); CHECK(strstr(sent, "QFDEL"));
    CHECK(!ABoxEnrollmentEc800Http_Ready(&http));
    /* Every HTTP wait point, both queued and active, including duplicate reset.
     * CA operations are separately reset and must be reverified. */
    for (unsigned mode = 0; mode < 3; ++mode) {
        for (unsigned step = 0; step < 28; ++step) {
            Result reset_result = {0};
            CHECK(ABoxEc800At_Init(&at, &at_port));
            CHECK(ABoxEnrollmentEc800Http_Init(&http, &at, 18U,
                "UFS:ota_ca.pem", done, &reset_result));
            CHECK(ABoxEnrollmentEc800Http_Post(&http, "https://ota.example/poll", "{}", response, sizeof(response)));
            for (unsigned n = 0; n < step / 2; ++n) {
                ABoxEc800At_Task(&at);
                if (strstr(sent, "QHTTPURL") || strstr(sent, "QHTTPPOST")) feed(&at, "CONNECT\r\n");
                if (strstr(at.active.command, "QHTTPPOST")) feed(&at, "OK\r\n+QHTTPPOST: 0,201,2\r\n");
                else feed(&at, "OK\r\n");
            }
            if (step & 1U) ABoxEc800At_Task(&at);
            if (!mode) feed(&at, "RDY\r\n");
            else if (mode == 1) ABoxEc800At_AbortAll(&at, ABOX_EC800_RESULT_ERROR);
            else ABoxEc800At_Quarantine(&at);
            CHECK(http.state == ABOX_ENROLL_HTTP_IDLE);
            CHECK(reset_result.calls == 1U && !reset_result.length &&
                  (!reset_result.status || !reset_result.clean));
            feed(&at, "RDY\r\nRDY\r\n");
            CHECK(reset_result.calls == 1U);
            CHECK(ABoxEnrollmentEc800Http_Ready(&http));
        }
    }
    for (unsigned payload=0;payload<2;++payload) {
        Result failure={0};
        CHECK(ABoxEc800At_Init(&at,&at_port));
        CHECK(ABoxEnrollmentEc800Http_Init(&http,&at,18,"UFS:ota_ca.pem",done,&failure));
        CHECK(ABoxEnrollmentEc800Http_Post(&http,"https://ota.example/poll","{}",response,sizeof(response)));
        if (payload) {
            for (unsigned n=0;n<11;++n) { ABoxEc800At_Task(&at); feed(&at,"OK\r\n"); }
            ABoxEc800At_Task(&at); CHECK(strstr(sent,"QHTTPURL"));
        }
        fail_write=1;
        if (payload) feed(&at,"CONNECT\r\n");
        else ABoxEc800At_Task(&at);
        CHECK(at.quarantined && http.state==ABOX_ENROLL_HTTP_IDLE && failure.calls==1);
        CHECK(!failure.status && !failure.length);
        feed(&at,"OK\r\n"); CHECK(failure.calls==1 && !ABoxEc800At_IsBusy(&at));
        fail_write=0; feed(&at,"RDY\r\nRDY\r\n");
        CHECK(failure.calls==1 && ABoxEnrollmentEc800Http_Ready(&http));
    }
    puts("EC800 HTTPS cleanup, dedicated CA and reset/abort/TX failure termination passed");
    return 0;
}
