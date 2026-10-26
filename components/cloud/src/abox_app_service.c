#include "abox_app_service.h"
#include "abox_boot_v2_app.h"
#include "ec_power.h"
#include <string.h>
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(ABoxAppService) == 16U, "AppService exceeds existing ARM BSS budget");
#endif
static void receive(void *context, const uint8_t *data, uint16_t length)
{ ABoxEc800At_Feed((ABoxEc800At *)context, data, length); }
int ABoxAppService_Init(ABoxAppService *service, ABoxEc800At *at,
    ABoxEc800Rx *rx, const ABoxAppServicePort *port)
{
    if (!service || !at || !port || !port->cloud || (rx && !port->restart_rx) ||
        (port->recovery && (!port->transport_error || !port->maintenance_busy || !port->event))) return 0;
    memset(service, 0, sizeof(*service));
    service->at = at; service->rx = rx; service->port = port;
    if (rx) {
        service->rx_starting = 1U;
        ABoxEc800Rx_RequestRestart(rx);
    }
    return 1;
}
void ABoxAppService_Event(ABoxAppService *service, ABoxAppServiceEvent event)
{
    if (service && service->port && service->port->event)
        service->port->event(service->port->context, event);
}
static int recover(ABoxAppService *service, uint32_t now)
{
    const ABoxAppServicePort *p = service->port;
    if (!p->recovery || !ABoxEc800Recovery_Poll(p->recovery, now,
            service->at->quarantined || p->transport_error(p->context), EC_Power_IsOnDone() &&
            !ABoxBootV2App_IsBusy() && !p->maintenance_busy(p->context))) return 0;
    ABoxAppService_Event(service, ABOX_APP_NETWORK_CHANGED);
    ABoxEc800At_AbortAll(service->at, ABOX_EC800_RESULT_ERROR);
    EC_Power_StartRestartSequence();
    service->reset_pending = 1U;
    if (p->restarted) p->restarted(p->context, now);
    return 1;
}
void ABoxAppService_Poll(ABoxAppService *service, uint32_t now)
{
    const ABoxAppServicePort *p;
    int recovery_checked = 0, communication_enabled = 0;
    if (!service || !service->at || !service->port || service->polling) return;
    service->polling = 1U;
    p = service->port;
    EC_Power_Task();
    if (service->reset_pending && EC_Power_IsOnDone()) {
        /* A locked MQTT/raw parser can swallow RDY. The completed rail cycle
         * is independent reset evidence; discard old RX and notify all owners
         * so both parsers and upper state machines converge without re-init. */
        if (service->rx) {
            if (!service->rx_starting) {
                service->rx_starting = 1U;
                service->rx_failed = 0U;
                ABoxEc800Rx_RequestRestart(service->rx);
            }
        } else {
            service->reset_pending = 0U;
            ABoxEc800At_OnModemReset(service->at);
        }
    }
    if (service->rx) {
        ABoxEc800At_SetRxOverflowCount(service->at, ABoxEc800Rx_OverflowCount(service->rx));
        if (ABoxEc800Rx_TakeRestartRequest(service->rx)) {
            if (!service->rx_failed) {
                if (!service->rx_starting)
                    ABoxEc800At_AbortAll(service->at, ABOX_EC800_RESULT_ERROR);
                service->rx_failed = 1U;
                service->rx_retry_at = (uint16_t)(now >> 4);
            }
        }
        if (service->rx_failed) {
            if ((int16_t)((uint16_t)(now >> 4) - service->rx_retry_at) >= 0) {
                if (p->restart_rx(p->context) && !service->rx->restart_requested) {
                    service->rx_failed = 0U;
                    service->rx_starting = 0U;
                    /* A completed rail cycle is not usable until RX is armed.
                     * Keep the reset evidence across local UART retries. */
                    if (service->reset_pending && EC_Power_IsOnDone()) {
                        service->reset_pending = 0U;
                        ABoxEc800At_OnModemReset(service->at);
                    }
                } else {
                    ABoxEc800At_AbortAll(service->at, ABOX_EC800_RESULT_ERROR);
                    service->rx_retry_at = (uint16_t)((now >> 4) + 8U);
                }
            }
            if (service->rx_failed) {
                ABoxEc800Rx_RequestRestart(service->rx);
                communication_enabled = !p->enabled || p->enabled(p->context);
                if (p->ready) p->ready(p->context, 0, now);
                goto done;
            }
        }
        (void)ABoxEc800Rx_Drain(service->rx, receive, service->at, 0U);
    }
    if (!p->enabled || p->enabled(p->context)) {
        communication_enabled = 1;
        ABoxEc800At_Task(service->at);
        if (p->after_at) p->after_at(p->context, now);
        if (p->ota) p->ota(p->context, now);
        else ABoxBootV2App_Task();
        if (p->before_enrollment) p->before_enrollment(p->context, now);
        if (p->enrollment) p->enrollment(p->context, now);
        if (p->before_cloud && !p->before_cloud(p->context, now)) goto done;
        if (p->can_connect && !p->can_connect(p->context)) goto done;
        if (p->transport_first && p->transport) p->transport(p->context, now);
        if (p->runtime) p->runtime(p->context, now);
        if (p->runtime_ready && !p->runtime_ready(p->context)) {
            if (p->ready) p->ready(p->context, 0, now);
            goto done;
        }
        if (!p->transport_first && p->transport) p->transport(p->context, now);
        if (!p->recovery_after_cloud) {
            recovery_checked = 1;
            if (recover(service, now)) goto done;
        }
        if (p->ready) p->ready(p->context,
            p->transport_ready ? p->transport_ready(p->context) : 1, now);
        p->cloud(p->context, now);
    }
done:
    /* Runtime readiness and enrollment/config gates must not hide physical
     * transport recovery. Maintenance/OTA admission remains in recover(). */
    if (communication_enabled && !recovery_checked) (void)recover(service, now);
    service->polling = 0U;
}
