#include "abox_boot_v2_ec800_port.h"
#include "abox_boot_v2_services.h"

#include <assert.h>
#include <string.h>

static char command[128];
static ABoxBootV2AtResult completion;
static unsigned completed;
static unsigned lines;

static uint32_t tick(void *context) { (void)context; return 123U; }
static int write_bytes(void *context, const uint8_t *data, uint16_t length,
                       uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    assert(length < sizeof(command));
    memcpy(command, data, length);
    command[length] = 0;
    return 1;
}
static void done(ABoxBootV2AtResult result, void *user)
{
    assert(user == &completed);
    completion = result;
    ++completed;
}
static void event(ABoxBootV2AtEvent kind, const uint8_t *data,
                  uint16_t length, void *user)
{
    assert(user == &lines);
    assert(kind == ABOX_BOOT_V2_AT_LINE);
    if (length == 9U && memcmp(data, "+QHTTP: 1", 9U) == 0)
        ++lines;
}
static int product_stop(void *context, uint32_t now)
{
    (void)context;
    return now == 123U;
}

static void message(void *context, const char *topic, const char *payload)
{ (void)context; (void)topic; (void)payload; }

int main(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort at_port = {0, tick, write_bytes, 0};
    ABoxBootV2AppPort port = {0};
    ABoxBootV2Ec800Adapter adapter;
    const uint8_t reply[] = "OK\r\n+QHTTPGET: 0,200,16\r\n";
    const uint8_t urc[] = "+QHTTP: 1\r\n";

    assert(ABoxEc800At_Init(&at, &at_port));
    port.tick_ms = tick;
    port.mqtt_stop = product_stop;
    assert(!ABoxBootV2Ec800Adapter_Bind(0, &at, &port));
    assert(ABoxBootV2Ec800Adapter_Bind(&adapter, &at, &port));
    assert(port.context == &adapter && port.tick_ms == tick &&
           port.mqtt_stop == product_stop);
    assert(port.mqtt_stop(port.context, 123U));
    assert(port.register_events(port.context, event, &lines));
    assert(port.submit(port.context, "AT+QHTTPGET=80", 1000U,
                       done, &completed));
    assert(port.has_pending(port.context));
    assert(!port.submit(port.context, "AT+QHTTPGET=90", 1000U,
                        done, &completed));
    ABoxEc800At_Task(&at);
    assert(strstr(command, "AT+QHTTPGET=80") != 0);
    assert(port.is_active(port.context));
    ABoxEc800At_Feed(&at, reply, sizeof(reply) - 1U);
    assert(completed == 1U && completion == ABOX_BOOT_V2_AT_OK);
    assert(!port.is_active(port.context));
    ABoxEc800At_Feed(&at, urc, sizeof(urc) - 1U);
    assert(lines == 1U);
    ABoxEc800At_SetRxOverflowCount(&at, 7U);
    assert(port.rx_overflow_count(port.context) == 7U);
    assert(port.submit(port.context, "AT+QHTTPGET=90", 1000U,
                       done, &completed));
    port.cancel(port.context);
    assert(!port.has_pending(port.context));
    assert(completed == 1U);
    ABoxMqttEc800Bridge mqtt = {0};
    ABoxMqttEc800Config config = {0};
    ABoxMqttEc800Buffers buffers = {0};
    const ABoxMqttEc800BridgeHooks hooks = {.message=message};
    uint8_t header[192], topic[128], payload[1024], outgoing[128];
    assert(!ABoxBootV2Ec800Adapter_BindServices(&adapter, &at, &mqtt, &port));
    config.apn="CMNET"; config.host="broker.example"; config.port=1883U;
    config.client_id="device"; config.client_index=1U;
    config.command_timeout_ms=100U; config.open_timeout_ms=1000U;
    config.connect_timeout_ms=1000U; config.subscribe_timeout_ms=1000U;
    config.publish_timeout_ms=1000U; config.retry_delay_ms=100U;
    buffers.header=header; buffers.header_capacity=sizeof(header);
    buffers.topic=topic; buffers.topic_capacity=sizeof(topic);
    buffers.payload=payload; buffers.payload_capacity=sizeof(payload);
    buffers.publish_payload=outgoing; buffers.publish_capacity=sizeof(outgoing);
    assert(ABoxMqttEc800Bridge_Init(&mqtt, &at, &config, &buffers, &hooks,
                                     ABOX_EC800_OWNER_PRODUCT_BASE + 5U, 123U));
    assert(ABoxBootV2Ec800Adapter_BindServices(&adapter, &at, &mqtt, &port));
    assert(port.tick_ms(port.context) == 123U);
    assert(port.mqtt_client_index(port.context) == 1U);
    assert(port.transfer_buffer == payload && port.transfer_buffer_size == sizeof(payload));
    assert(!port.workspace_borrow(port.context)); /* cannot borrow an active session */
    mqtt.transport.state=ABOX_MQTT_EC800_PAUSED; /* completed transport stop */
    assert(port.workspace_borrow(port.context));
    assert(!port.workspace_borrow(port.context)); /* one lease across repeated OTA polls */
    assert(!ABoxMqttEc800_Resume(&mqtt.transport, 123U));
    port.workspace_return(port.context);
    assert(!mqtt.transport.workspace_borrowed);
    uint8_t dedicated[1024];
    port.transfer_buffer=dedicated; port.transfer_buffer_size=sizeof(dedicated);
    assert(ABoxBootV2Ec800Adapter_BindServices(&adapter, &at, &mqtt, &port));
    assert(port.transfer_buffer == dedicated); /* preserve product RAM allocation */
    return 0;
}
