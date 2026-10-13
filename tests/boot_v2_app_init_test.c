/* Include the real singleton implementation to poison only NOLOAD scratch,
 * without adding a production test hook or treating host BSS as NOLOAD RAM. */
#include "../components/boot_v2/src/abox_boot_v2_app.c"
#include "abox_boot_v2_services.h"
#include <assert.h>

static uint8_t buffer[ABOX_BOOT_V2_APP_RAW_CHUNK];
static unsigned effects;
static unsigned returns;
static int fail_write;
static ABoxBootV2AtEventFn registered_event;
static int submit_test(void *context, const char *command, uint32_t timeout,
                       ABoxBootV2AtDoneFn done, void *user)
{
    (void)context; (void)command; (void)timeout; (void)done; (void)user;
    ++effects;
    return 1;
}
static int register_test(void *context, ABoxBootV2AtEventFn callback, void *user)
{ (void)context; (void)user; registered_event = callback; ++effects; return 1; }
static int reject_registration(void *context, ABoxBootV2AtEventFn callback, void *user)
{ (void)context; (void)callback; (void)user; return 0; }
static void occupied(ABoxEc800Event e, const uint8_t *d, uint16_t n, void *u)
{ (void)e; (void)d; (void)n; (void)u; }
static uint8_t client_test(void *context) { (void)context; ++effects; return 0U; }
static uint32_t tick_test(void *context) { (void)context; ++effects; return 1000U; }
static int write_test(void *context, const uint8_t *data, uint16_t n, uint32_t t)
{ (void)context; (void)data; (void)n; (void)t; return !fail_write; }
static int stop_test(void *context, uint32_t t) { (void)context; (void)t; return 1; }
static int borrow_test(void *context) { (void)context; return 1; }
static void return_test(void *context) { (void)context; ++returns; }

static const ABoxBootV2AppPort port = {
    .tick_ms = tick_test, .submit = submit_test, .register_events = register_test,
    .mqtt_client_index = client_test, .transfer_buffer = buffer,
    .transfer_buffer_size = sizeof(buffer)
};
static const ABoxBootV2AppConfig config = {
    "https://ota.example", "test", "app.bin", "test-", "test-current",
    (const uint8_t *)"CA", 2U, 0x08008000U, 128U, 0x36000U
};

static void check_disabled(void)
{
    Context before;
    uint32_t error = 0U;
    unsigned before_effects = effects;
    memcpy(&before, &g, sizeof(g));
    assert(!ABoxBootV2App_IsInitialized());
    ABoxBootV2App_Task();
    ABoxBootV2App_OnModemReset();
    assert(!ABoxBootV2App_Start((const ABoxBootV2AppRequest *)(uintptr_t)1U));
    assert(!ABoxBootV2App_BeginProvisioning());
    assert(!ABoxBootV2App_IsBusy());
    assert(!ABoxBootV2App_IsProvisioned());
    assert(!ABoxBootV2App_IsReady());
    assert(!ABoxBootV2App_TakeInstallReady());
    assert(!ABoxBootV2App_TakeFailure(&error));
    assert(error == ABOX_BOOT_V2_APP_ERROR_STATE);
    assert(ABoxBootV2App_LastError() == ABOX_BOOT_V2_APP_ERROR_STATE);
    assert(!strcmp(ABoxBootV2App_Phase(), "INIT_FAILED"));
    assert(!strcmp(ABoxBootV2App_TargetVersion(), ""));
    assert(!ABoxBootV2App_DownloadMetrics());
    assert(!strcmp(ABoxBootV2App_StableSeedSource(), "local_flash"));
    /* Delayed callbacks from a previous binding must also be inert. */
    command_done(ABOX_BOOT_V2_AT_OK, NULL);
    downloader_bridge_done(ABOX_BOOT_V2_AT_ERROR, NULL);
    event_received(ABOX_BOOT_V2_AT_RAW, (const uint8_t *)(uintptr_t)1U, 16U, NULL);
    assert(effects == before_effects);
    assert(!memcmp(&before, &g, sizeof(g)));
}

int main(void)
{
    ABoxBootV2AppConfig invalid = config;
    ABoxBootV2AppPort invalid_port = port;
    ABoxBootV2Ec800Adapter adapter;
    ABoxEc800At at = {0};
    ABoxMqttEc800Bridge mqtt = {0};
    ABoxBootV2AppPort unbound = {0};
    char oversized_version[ABOX_BOOT_V2_APP_VERSION_SIZE + 1U];
    memset(&g, 0xA5, sizeof(g));
    check_disabled(); /* startup BSS ready flag is zero, scratch is poisoned */
    assert(!ABoxBootV2App_Init(NULL, &config));
    check_disabled();
    assert(!ABoxBootV2App_Init(&port, NULL));
    check_disabled();
    invalid.ota_origin = NULL;
    assert(!ABoxBootV2App_Init(&port, &invalid));
    check_disabled();
    invalid = config; invalid.app_size = config.app_max_size + 1U;
    assert(!ABoxBootV2App_Init(&port, &invalid));
    check_disabled();
    memset(oversized_version, 'x', sizeof(oversized_version) - 1U);
    oversized_version[sizeof(oversized_version) - 1U] = '\0';
    invalid = config; invalid.running_version = oversized_version;
    assert(!ABoxBootV2App_Init(&port, &invalid));
    check_disabled();
    invalid_port.mqtt_stop = (int (*)(void *, uint32_t))(uintptr_t)1U;
    assert(!ABoxBootV2App_Init(&invalid_port, &config));
    check_disabled();
    assert(effects == 0U && !registered_event);
    invalid_port = port; invalid_port.register_events = reject_registration;
    memset(&g, 0xA5, sizeof(g));
    assert(!ABoxBootV2App_Init(&invalid_port, &config));
    check_disabled();
    at.port.tick_ms = tick_test;
    assert(!ABoxBootV2Ec800Adapter_BindServices(&adapter, &at, &mqtt, &unbound));
    assert(!ABoxBootV2App_Init(&unbound, &config));
    check_disabled();

    {
        const ABoxEc800AtPort wire = {.tick_ms=tick_test,.write=write_test};
        ABoxBootV2AppPort actual = port;
        assert(ABoxEc800At_Init(&at,&wire));
        for (unsigned i=0;i<ABOX_EC800_AT_HANDLER_SIZE;++i)
            assert(ABoxEc800At_Register(&at,(ABoxEc800Owner)(20+i),occupied,NULL));
        assert(ABoxBootV2Ec800Adapter_Bind(&adapter,&at,&actual));
        assert(!ABoxBootV2App_Init(&actual,&config));
        check_disabled(); /* Real full registration table, poisoned old g. */
    }

    assert(ABoxBootV2App_Init(&port, &config));
    assert(ABoxBootV2App_IsInitialized() && effects == 1U && registered_event);
    assert(!strcmp(ABoxBootV2App_Phase(), "WAIT_MODEM"));
    assert(ABoxBootV2App_DownloadMetrics());
    assert(ABoxBootV2App_BeginProvisioning());
    assert(effects == 2U);
    /* Simulate a later startup/idle rebind failure, retaining corrupted RAM. */
    memset(&g, 0x5A, sizeof(g));
    assert(!ABoxBootV2App_Init(&port, &invalid));
    check_disabled();
    registered_event(ABOX_BOOT_V2_AT_LINE, (const uint8_t *)"RDY", 3U, NULL);
    check_disabled();
    assert(ABoxBootV2App_Init(&port, &config));
    assert(ABoxBootV2App_IsInitialized());
    for (unsigned mode=0; mode<3; ++mode) for (unsigned active=0; active<2; ++active) {
        ABoxBootV2AppPort actual = port;
        ABoxEc800AtPort wire = {.tick_ms=tick_test, .write=write_test};
        assert(ABoxEc800At_Init(&at, &wire));
        assert(ABoxBootV2Ec800Adapter_Bind(&adapter, &at, &actual));
        actual.mqtt_stop=stop_test; actual.workspace_borrow=borrow_test;
        actual.workspace_return=return_test;
        assert(ABoxBootV2App_Init(&actual, &config));
        g.busy=g.provisioned=g.workspace_borrowed=g.handle_valid=1U;
        g.state=ST_VERIFY_CLOSE;
        returns=0;
        assert(actual.submit(actual.context, "AT+QFCLOSE=1", 5000U, command_done, NULL));
        if (active) ABoxEc800At_Task(&at);
        if (!mode) ABoxEc800At_Feed(&at, (const uint8_t *)"RDY\r\n", 5U);
        else if (mode==1) ABoxEc800At_AbortAll(&at, ABOX_EC800_RESULT_ERROR);
        else ABoxEc800At_Quarantine(&at);
        assert(!ABoxBootV2App_IsBusy() && !adapter.done && returns==1U);
        uint32_t error;
        assert(ABoxBootV2App_TakeFailure(&error) && error==ABOX_BOOT_V2_APP_ERROR_UFS);
        assert(!ABoxBootV2App_TakeFailure(&error));
        ABoxEc800At_Feed(&at, (const uint8_t *)"RDY\r\nRDY\r\n", 10U);
        assert(!ABoxBootV2App_IsProvisioned() && g.state==ST_RETRY);
        assert(!ABoxBootV2App_IsBusy() && returns==1U);
        assert(!ABoxBootV2App_TakeFailure(&error));
        /* Reset also ends non-AT waits, e.g. MQTT workspace acquisition. */
        g.state=ST_MQTT_QUIESCE; g.busy=1U;
        ABoxBootV2App_OnModemReset();
        assert(!ABoxBootV2App_IsBusy() && returns==1U);
    }
    {
        const ABoxEc800AtPort wire={.tick_ms=tick_test,.write=write_test};
        ABoxBootV2AppPort actual=port;
        assert(ABoxEc800At_Init(&at,&wire));
        assert(ABoxBootV2Ec800Adapter_Bind(&adapter,&at,&actual));
        actual.mqtt_stop=stop_test; actual.workspace_borrow=borrow_test; actual.workspace_return=return_test;
        assert(ABoxBootV2App_Init(&actual,&config));
        g.busy=g.provisioned=g.workspace_borrowed=g.seed_attempted=1U;
        g.state=ST_URL; strcpy(g.url,"https://ota.example/app.bin"); returns=0;
        assert(actual.submit(actual.context,"AT+QHTTPURL=27,80",5000,command_done,NULL));
        ABoxEc800At_Task(&at); ABoxEc800At_Feed(&at,(const uint8_t *)"CONNECT\r\n",9);
        fail_write=1; ABoxBootV2App_Task(); fail_write=0;
        assert(at.quarantined && !ABoxBootV2App_IsBusy() && !adapter.done && returns==1);
        uint32_t error;
        assert(ABoxBootV2App_TakeFailure(&error) && !ABoxBootV2App_TakeFailure(&error));
        ABoxEc800At_Feed(&at,(const uint8_t *)"OK\r\nRDY\r\nRDY\r\n",14);
        assert(!ABoxBootV2App_IsBusy() && returns==1 && !ABoxBootV2App_TakeFailure(&error));
    }
    return 0;
}
