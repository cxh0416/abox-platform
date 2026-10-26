#ifndef ABOX_APP_SERVICE_H
#define ABOX_APP_SERVICE_H
#include "abox_ec800_at.h"
#include "abox_ec800_rx.h"
#include "abox_ec800_recovery.h"
#ifdef __cplusplus
extern "C" {
#endif
/* All callbacks run in one communication owner. IRQs only feed the supplied
 * RX ring/request restart; they must never call Poll or Event. Store the port
 * in Flash. Product callbacks supply policy, not additional AT owners. */
typedef enum { ABOX_APP_NETWORK_CHANGED, ABOX_APP_MODEM_READY } ABoxAppServiceEvent;
typedef struct {
    void *context;
    /* Stop/abort must succeed before clearing RX cursors; return success only
     * after ReceiveToIdle has started. A failed attempt is retried locally. */
    int (*restart_rx)(void *);
    int (*enabled)(void *);
    void (*after_at)(void *, uint32_t);
    void (*before_enrollment)(void *, uint32_t);
    void (*enrollment)(void *, uint32_t);
    void (*cloud)(void *, uint32_t);
    void (*event)(void *, ABoxAppServiceEvent);
    int (*before_cloud)(void *, uint32_t); /* Zero holds transport during maintenance. */
    int (*can_connect)(void *);
    void (*runtime)(void *, uint32_t);
    int (*runtime_ready)(void *);
    void (*transport)(void *, uint32_t);
    int (*transport_ready)(void *);
    void (*ready)(void *, int, uint32_t);
    int (*transport_error)(void *);
    int (*maintenance_busy)(void *);
    ABoxEc800Recovery *recovery; /* Caller-owned, initialized before binding. */
    void (*restarted)(void *, uint32_t);
    uint8_t transport_first;
    uint8_t recovery_after_cloud;
    /* Optional OTA adapter for a product-owned workspace lease. When omitted,
     * the standard Boot V2 task is polled. Never call both from the product. */
    void (*ota)(void *, uint32_t);
} ABoxAppServicePort;
typedef struct {
    ABoxEc800At *at;
    ABoxEc800Rx *rx;
    const ABoxAppServicePort *port;
    uint8_t polling : 1;
    uint8_t reset_pending : 1;
    uint8_t rx_failed : 1;
    uint8_t rx_starting : 1;
    /* 16 ms units, 128 ms retry. The owner polls within the half-wrap (524 s).
     * Fits existing ARM struct padding: no additional product BSS. */
    uint16_t rx_retry_at;
} ABoxAppService;
int ABoxAppService_Init(ABoxAppService *service, ABoxEc800At *at,
    ABoxEc800Rx *rx, const ABoxAppServicePort *port);
void ABoxAppService_Poll(ABoxAppService *service, uint32_t now);
void ABoxAppService_Event(ABoxAppService *service, ABoxAppServiceEvent event);
#ifdef __cplusplus
}
#endif
#endif
