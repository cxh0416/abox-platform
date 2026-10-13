#include "abox_boot_v2_ec800_port.h"

#include <string.h>

_Static_assert((int)ABOX_EC800_RESULT_OK == (int)ABOX_BOOT_V2_AT_OK &&
               (int)ABOX_EC800_RESULT_ERROR == (int)ABOX_BOOT_V2_AT_ERROR &&
               (int)ABOX_EC800_RESULT_TIMEOUT == (int)ABOX_BOOT_V2_AT_TIMEOUT &&
               (int)ABOX_EC800_RESULT_SEND_OK == (int)ABOX_BOOT_V2_AT_SEND_OK &&
               (int)ABOX_EC800_RESULT_SEND_FAIL == (int)ABOX_BOOT_V2_AT_SEND_FAIL,
               "EC800 and Boot V2 AT result values must match");
_Static_assert((int)ABOX_EC800_EVENT_LINE == (int)ABOX_BOOT_V2_AT_LINE &&
               (int)ABOX_EC800_EVENT_RAW == (int)ABOX_BOOT_V2_AT_RAW,
               "EC800 and Boot V2 event values must match");

static void command_done(ABoxEc800Result result, void *user)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)user;
    ABoxBootV2AtDoneFn done = adapter->done;
    void *done_user = adapter->done_user;
    adapter->done = 0;
    adapter->done_user = 0;
    if (done) done((ABoxBootV2AtResult)result, done_user);
}

static int submit(void *context, const char *command, uint32_t timeout_ms,
                  ABoxBootV2AtDoneFn done, void *user)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    if (!adapter || !adapter->at || adapter->done || !command) return 0;
    adapter->done = done;
    adapter->done_user = user;
    if (!ABoxEc800At_Submit(adapter->at, command, ABOX_EC800_OWNER_OTA,
                             ABOX_EC800_PRIORITY_HIGH, timeout_ms,
                             command_done, adapter)) {
        adapter->done = 0;
        adapter->done_user = 0;
        return 0;
    }
    return 1;
}

static int send_payload(void *context, const uint8_t *data, uint16_t length)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    return adapter && adapter->at &&
           ABoxEc800At_SendPayload(adapter->at, ABOX_EC800_OWNER_OTA,
                                   data, length);
}

static int begin_raw(void *context, uint32_t length)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    return adapter && adapter->at &&
           ABoxEc800At_BeginRaw(adapter->at, ABOX_EC800_OWNER_OTA, length);
}

static int has_pending(void *context)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    return adapter && adapter->at &&
           ABoxEc800At_HasPending(adapter->at, ABOX_EC800_OWNER_OTA);
}

static int is_active(void *context)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    return adapter && adapter->at &&
           ABoxEc800At_IsActive(adapter->at, ABOX_EC800_OWNER_OTA);
}

static void cancel(void *context)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    if (!adapter || !adapter->at) return;
    ABoxEc800At_Cancel(adapter->at, ABOX_EC800_OWNER_OTA);
    adapter->done = 0;
    adapter->done_user = 0;
}

static void event(ABoxEc800Event kind, const uint8_t *data,
                  uint16_t length, void *user)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)user;
    if (adapter->events)
        adapter->events((ABoxBootV2AtEvent)kind, data, length,
                        adapter->events_user);
}

static int register_events(void *context, ABoxBootV2AtEventFn callback,
                            void *user)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    if (!adapter || !adapter->at || !callback) return 0;
    if (!ABoxEc800At_Register(adapter->at, ABOX_EC800_OWNER_OTA,
                              event, adapter)) return 0;
    adapter->events = callback;
    adapter->events_user = user;
    return 1;
}

static uint32_t rx_overflow(void *context)
{
    ABoxBootV2Ec800Adapter *adapter = (ABoxBootV2Ec800Adapter *)context;
    return adapter && adapter->at ? ABoxEc800At_RxOverflowCount(adapter->at) : 0U;
}

int ABoxBootV2Ec800Adapter_Bind(ABoxBootV2Ec800Adapter *adapter,
                                ABoxEc800At *at, ABoxBootV2AppPort *port)
{
    if (!adapter || !at || !port) return 0;
    memset(adapter, 0, sizeof(*adapter));
    adapter->at = at;
    port->context = adapter;
    port->submit = submit;
    port->send_payload = send_payload;
    port->begin_raw_read = begin_raw;
    port->has_pending = has_pending;
    port->is_active = is_active;
    port->cancel = cancel;
    port->register_events = register_events;
    port->rx_overflow_count = rx_overflow;
    return 1;
}
