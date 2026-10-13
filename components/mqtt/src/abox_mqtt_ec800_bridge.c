#include "abox_mqtt_ec800_bridge.h"

#include <string.h>

static void on_message(void *user, const uint8_t *topic, size_t topic_length,
                       const uint8_t *payload, size_t payload_length)
{
    ABoxMqttEc800Bridge *bridge = (ABoxMqttEc800Bridge *)user;
    const ABoxMqttEc800Buffers *buffers;
    if (!bridge || !bridge->hooks || !bridge->hooks->message) return;
    buffers = &bridge->transport.buffers;
    if (!topic || !payload || !buffers->topic || !buffers->payload ||
        topic != buffers->topic || payload != buffers->payload ||
        topic_length >= buffers->topic_capacity ||
        payload_length >= buffers->payload_capacity ||
        memchr(topic, 0, topic_length) || memchr(payload, 0, payload_length))
        return;
    buffers->topic[topic_length] = 0;
    buffers->payload[payload_length] = 0;
    bridge->hooks->message(bridge->hooks->context, (const char *)topic,
                           (const char *)payload);
}

static void on_publish(void *user, uint64_t operation,
                       ABoxMqttEc800PublishEvent event)
{
    ABoxMqttEc800Bridge *bridge = (ABoxMqttEc800Bridge *)user;
    ABoxMqttReceipt_OnEvent(&bridge->receipt, operation, event);
    if (event == ABOX_MQTT_EC800_PUBLISH_CONFIRMED) ++bridge->publish_ok;
    if (bridge->hooks->publish_event)
        bridge->hooks->publish_event(bridge->hooks->context, operation, event);
}

static void on_state(void *user, ABoxMqttEc800State state)
{
    ABoxMqttEc800Bridge *bridge = (ABoxMqttEc800Bridge *)user;
    if (bridge->last_state == ABOX_MQTT_EC800_READY &&
        state != ABOX_MQTT_EC800_READY && bridge->hooks->network_changed)
        bridge->hooks->network_changed(bridge->hooks->context);
    bridge->last_state = state;
    if (bridge->hooks->state_changed)
        bridge->hooks->state_changed(bridge->hooks->context, state);
}

static void on_modem_reset(void *user)
{
    ABoxMqttEc800Bridge *bridge = (ABoxMqttEc800Bridge *)user;
    bridge->iccid_requested = 0U;
    if (bridge->hooks->modem_reset)
        bridge->hooks->modem_reset(bridge->hooks->context);
}

int ABoxMqttEc800Bridge_Init(ABoxMqttEc800Bridge *bridge, ABoxEc800At *at,
                             const ABoxMqttEc800Config *config,
                             const ABoxMqttEc800Buffers *buffers,
                             const ABoxMqttEc800BridgeHooks *hooks,
                             ABoxEc800Owner iccid_owner, uint32_t now_ms)
{
    ABoxMqttEc800Callbacks callbacks;
    if (!bridge || !at || !config || !buffers || !hooks || !hooks->message ||
        !buffers->topic || !buffers->payload || !buffers->header ||
        !buffers->publish_payload || iccid_owner == ABOX_EC800_OWNER_NONE ||
        iccid_owner == ABOX_EC800_OWNER_MQTT) return 0;
    memset(bridge, 0, sizeof(*bridge));
    bridge->at = at;
    bridge->hooks = hooks;
    bridge->last_state = ABOX_MQTT_EC800_IDLE;
    ABoxMqttReceipt_Init(&bridge->receipt);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.user = bridge;
    callbacks.message = on_message;
    callbacks.publish_event = on_publish;
    callbacks.state_changed = on_state;
    callbacks.modem_reset = on_modem_reset;
    if (!ABoxMqttEc800_Init(&bridge->transport, at, config, buffers,
                             &callbacks, now_ms)) return 0;
    if (!ABoxEc800Iccid_Init(&bridge->iccid, at, iccid_owner)) {
        (void)ABoxEc800At_Register(at, ABOX_EC800_OWNER_MQTT, NULL, NULL);
        (void)ABoxEc800At_SetMqttReceiver(at, NULL);
        bridge->transport.initialized = 0U;
        return 0;
    }
    bridge->initialized = 1U;
    return 1;
}

void ABoxMqttEc800Bridge_Poll(ABoxMqttEc800Bridge *bridge, uint32_t now_ms)
{
    if (!bridge || !bridge->initialized) return;
    ABoxMqttEc800_Poll(&bridge->transport, now_ms);
    ABoxEc800Iccid_Poll(&bridge->iccid, now_ms);
    if (bridge->iccid_requested &&
        ABoxMqttEc800Bridge_CanRunBackgroundAt(bridge) &&
        ABoxEc800Iccid_Request(&bridge->iccid, now_ms))
        bridge->iccid_requested = 0U;
}

void ABoxMqttEc800Bridge_OnModemReset(ABoxMqttEc800Bridge *bridge,
                                       uint32_t now_ms)
{
    if (!bridge || !bridge->initialized) return;
    ABoxMqttReceipt_Invalidate(&bridge->receipt);
    ABoxMqttEc800_OnModemReset(&bridge->transport, now_ms);
    ABoxEc800Iccid_OnModemReset(&bridge->iccid);
    bridge->iccid_requested = 0U;
}

void ABoxMqttEc800Bridge_SetPaused(ABoxMqttEc800Bridge *bridge,
                                    uint8_t paused, uint32_t now_ms)
{
    ABoxMqttEc800State state;
    if (!bridge) return;
    bridge->paused = paused ? 1U : 0U;
    if (!bridge->initialized) return;
    ABoxMqttEc800_SetPausedSoft(&bridge->transport, bridge->paused);
    if (bridge->paused || !bridge->security_ready) return;
    state = ABoxMqttEc800_GetState(&bridge->transport);
    if (state == ABOX_MQTT_EC800_PAUSED)
        (void)ABoxMqttEc800_Resume(&bridge->transport, now_ms);
    else if (state == ABOX_MQTT_EC800_IDLE)
        (void)ABoxMqttEc800_Start(&bridge->transport, now_ms);
}

void ABoxMqttEc800Bridge_SetSecurityReady(ABoxMqttEc800Bridge *bridge,
                                           uint8_t ready)
{
    if (!bridge) return;
    bridge->security_ready = ready ? 1U : 0U;
    if (bridge->initialized)
        ABoxMqttEc800_SetSecurityReady(&bridge->transport,
                                         bridge->security_ready);
}

int ABoxMqttEc800Bridge_Configure(ABoxMqttEc800Bridge *bridge,
                                   const ABoxMqttEc800Config *config)
{
    ABoxMqttEc800State state;
    if (!bridge || !bridge->initialized || !config) return 0;
    state = ABoxMqttEc800_GetState(&bridge->transport);
    if (state != ABOX_MQTT_EC800_IDLE && state != ABOX_MQTT_EC800_PAUSED)
        return 0;
    return ABoxMqttEc800_Configure(&bridge->transport, config);
}

int ABoxMqttEc800Bridge_Publish(ABoxMqttEc800Bridge *bridge,
                                 const char *topic, const char *payload,
                                 uint8_t qos, uint8_t retain,
                                 uint64_t *operation)
{
    if (!bridge || !bridge->initialized || !topic || !payload || !operation ||
        bridge->receipt.status == ABOX_MQTT_RECEIPT_PENDING ||
        !ABoxMqttEc800_Publish(&bridge->transport, topic,
                                (const uint8_t *)payload, strlen(payload),
                                qos, retain, operation)) return 0;
    return ABoxMqttReceipt_Begin(&bridge->receipt, *operation);
}

void ABoxMqttEc800Bridge_CancelPublish(ABoxMqttEc800Bridge *bridge)
{
    if (bridge && bridge->initialized)
        ABoxMqttEc800_CancelPublish(&bridge->transport);
}

int ABoxMqttEc800Bridge_CanRunBackgroundAt(const ABoxMqttEc800Bridge *bridge)
{
    return bridge && bridge->initialized &&
           ABoxMqttEc800_IsReady(&bridge->transport) &&
           !bridge->transport.publish_active &&
           !ABoxEc800At_IsBusy(bridge->at) &&
           !ABoxMqttEc800Rx_IsCollecting(&bridge->transport.rx);
}

int ABoxMqttEc800Bridge_RequestIccid(ABoxMqttEc800Bridge *bridge,
                                       uint32_t now_ms)
{
    if (!bridge || !bridge->initialized) return 0;
    if (bridge->iccid.valid || bridge->iccid.pending) return 1;
    if (ABoxMqttEc800Bridge_CanRunBackgroundAt(bridge) &&
        ABoxEc800Iccid_Request(&bridge->iccid, now_ms))
        bridge->iccid_requested = 0U;
    else
        bridge->iccid_requested = 1U;
    return 1;
}

int ABoxMqttEc800Bridge_BorrowWorkspace(ABoxMqttEc800Bridge *bridge,
                                         size_t minimum_capacity)
{
    size_t capacity = 0U;
    uint8_t *workspace;
    if (!bridge || !bridge->initialized) return 0;
    workspace = ABoxMqttEc800_BorrowWorkspace(&bridge->transport, &capacity);
    if (!workspace) return 0;
    if (workspace == bridge->transport.buffers.payload &&
        capacity >= minimum_capacity) return 1;
    (void)ABoxMqttEc800_ReturnWorkspace(&bridge->transport);
    return 0;
}

void ABoxMqttEc800Bridge_ReturnWorkspace(ABoxMqttEc800Bridge *bridge)
{
    if (bridge && bridge->initialized)
        (void)ABoxMqttEc800_ReturnWorkspace(&bridge->transport);
}
