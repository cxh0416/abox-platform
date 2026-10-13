#ifndef ABOX_EC800_AT_H
#define ABOX_EC800_AT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABOX_EC800_AT_LINE_SIZE 1472U
#define ABOX_EC800_AT_COMMAND_SIZE 256U
#define ABOX_EC800_AT_QUEUE_SIZE 6U
#define ABOX_EC800_AT_HANDLER_SIZE 6U
#define ABOX_EC800_AT_RAW_MAX 1024U

typedef uint8_t ABoxEc800Owner;

#define ABOX_EC800_OWNER_NONE 0U
#define ABOX_EC800_OWNER_MQTT 1U
#define ABOX_EC800_OWNER_OTA 2U
#define ABOX_EC800_OWNER_PRODUCT_BASE 16U

typedef enum {
    ABOX_EC800_PRIORITY_NORMAL = 0,
    ABOX_EC800_PRIORITY_HIGH = 1
} ABoxEc800Priority;

typedef enum {
    ABOX_EC800_EVENT_LINE = 0,
    ABOX_EC800_EVENT_RAW
} ABoxEc800Event;

typedef enum {
    ABOX_EC800_RESULT_OK = 0,
    ABOX_EC800_RESULT_ERROR,
    ABOX_EC800_RESULT_TIMEOUT,
    ABOX_EC800_RESULT_SEND_OK,
    ABOX_EC800_RESULT_SEND_FAIL
} ABoxEc800Result;

typedef void (*ABoxEc800EventFn)(ABoxEc800Event event, const uint8_t *data,
                                 uint16_t length, void *user);
typedef void (*ABoxEc800DoneFn)(ABoxEc800Result result, void *user);
typedef ABoxEc800Owner (*ABoxEc800RouteFn)(void *context,
                                           const uint8_t *line,
                                           uint16_t length,
                                           ABoxEc800Owner active_owner);

/* Optional receive demultiplexer. Feed only complete MQTT URC bytes through
 * this port; OTA RAW ownership takes precedence. A locked parser quarantines
 * AT until the physical modem and both parsers are reset. */
typedef struct {
    void *context;
    void (*feed)(void *context, const uint8_t *data, uint16_t length,
                 uint32_t now_ms);
    void (*poll)(void *context, uint32_t now_ms);
    int (*collecting)(void *context);
    int (*locked)(void *context);
} ABoxEc800MqttRxPort;

typedef struct {
    void *context;
    uint32_t (*tick_ms)(void *context);
    int (*write)(void *context, const uint8_t *data, uint16_t length,
                 uint32_t timeout_ms);
    void (*log)(void *context, uint8_t level, const char *message);
    /* Return NONE to broadcast. A NULL router uses common AT routing. */
    ABoxEc800RouteFn route_line;
} ABoxEc800AtPort;

typedef struct {
    char command[ABOX_EC800_AT_COMMAND_SIZE];
    ABoxEc800Owner owner;
    ABoxEc800Priority priority;
    uint32_t timeout_ms;
    ABoxEc800DoneFn done;
    void *user;
    uint8_t used;
} ABoxEc800Command;

typedef struct {
    ABoxEc800Owner owner;
    ABoxEc800EventFn callback;
    void *user;
} ABoxEc800Handler;

typedef struct {
    ABoxEc800AtPort port;
    ABoxEc800MqttRxPort mqtt_rx;
    uint8_t line[ABOX_EC800_AT_LINE_SIZE];
    uint16_t line_length;
    ABoxEc800Command queue[ABOX_EC800_AT_QUEUE_SIZE];
    ABoxEc800Command active;
    ABoxEc800Handler handlers[ABOX_EC800_AT_HANDLER_SIZE];
    ABoxEc800Owner raw_owner;
    uint32_t raw_remaining;
    uint32_t active_tick;
    uint32_t abort_guard_tick;
    uint32_t rx_overflow_count;
    uint32_t line_overflow_count;
    /* Boolean gates share bytes; preserve the fixed product BSS budget. */
    uint8_t active_valid : 1;
    uint8_t active_payload_command : 1;
    uint8_t active_wait_payload : 1;
    uint8_t active_payload_sent : 1;
    uint8_t line_drop_until_lf : 1;
    uint8_t quarantined : 1;
    uint8_t mqtt_rx_active : 1;
    uint8_t terminating : 1;
    uint8_t driving : 1;
    uint8_t notifying : 2;
    uint32_t generation;
} ABoxEc800At;

int ABoxEc800At_Init(ABoxEc800At *at, const ABoxEc800AtPort *port);
int ABoxEc800At_SetMqttReceiver(ABoxEc800At *at,
                                 const ABoxEc800MqttRxPort *receiver);
/* Reset after a physical reset: pending commands receive ERROR once. RDY also
 * broadcasts LINE/RDY after completion. Callbacks may queue work normally,
 * but Submit is rejected during bulk termination; Task/Feed are not reentrant. */
void ABoxEc800At_Reset(ABoxEc800At *at);
/* Unified owner notification after observed RDY or a completed physical
 * power cycle. Clears local parsing then broadcasts the reset (LINE/RDY). */
void ABoxEc800At_OnModemReset(ABoxEc800At *at);
/* Opt-in fail-closed gate; cleared only by Reset after physical modem reset. */
void ABoxEc800At_Quarantine(ABoxEc800At *at);
void ABoxEc800At_Feed(ABoxEc800At *at, const uint8_t *data, uint16_t length);
void ABoxEc800At_Task(ABoxEc800At *at);
int ABoxEc800At_Submit(ABoxEc800At *at, const char *command,
                       ABoxEc800Owner owner, ABoxEc800Priority priority,
                       uint32_t timeout_ms, ABoxEc800DoneFn done, void *user);
int ABoxEc800At_SendPayload(ABoxEc800At *at, ABoxEc800Owner owner,
                            const uint8_t *data, uint16_t length);
/* A command/payload write failure may be partial: quarantine and complete all
 * accepted commands with SEND_FAIL exactly once. SendPayload may synchronously
 * invoke done; its caller must tolerate an already-terminated operation. */
int ABoxEc800At_ForcePayload(ABoxEc800At *at, ABoxEc800Owner owner,
                             const uint8_t *data, uint16_t length);
int ABoxEc800At_BeginRaw(ABoxEc800At *at, ABoxEc800Owner owner,
                         uint32_t length);
int ABoxEc800At_Register(ABoxEc800At *at, ABoxEc800Owner owner,
                         ABoxEc800EventFn callback, void *user);
int ABoxEc800At_HasPending(const ABoxEc800At *at, ABoxEc800Owner owner);
int ABoxEc800At_IsBusy(const ABoxEc800At *at);
int ABoxEc800At_IsActive(const ABoxEc800At *at, ABoxEc800Owner owner);
int ABoxEc800At_IsPayloadBusy(const ABoxEc800At *at);
void ABoxEc800At_CancelQueued(ABoxEc800At *at, ABoxEc800Owner owner);
void ABoxEc800At_Cancel(ABoxEc800At *at, ABoxEc800Owner owner);
void ABoxEc800At_CancelAll(ABoxEc800At *at);
/* AbortAll settles active AND queued commands once and quarantines the wire
 * until physical reset. Explicit Cancel APIs remain silent caller-owned cancel. */
void ABoxEc800At_AbortAll(ABoxEc800At *at, ABoxEc800Result result);
void ABoxEc800At_SetRxOverflowCount(ABoxEc800At *at, uint32_t count);
uint32_t ABoxEc800At_RxOverflowCount(const ABoxEc800At *at);
uint32_t ABoxEc800At_LineOverflowCount(const ABoxEc800At *at);

#ifdef __cplusplus
}
#endif
#endif
