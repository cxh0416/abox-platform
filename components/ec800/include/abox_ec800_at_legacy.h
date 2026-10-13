#ifndef ABOX_EC800_AT_LEGACY_H
#define ABOX_EC800_AT_LEGACY_H

/*
 * Source compatibility for the four Boot V2 products while their business
 * modules keep the established EC800_AT_* names.  New platform code should
 * include abox_ec800_at.h and use ABoxEc800At_* directly.
 */
#include "abox_ec800_at.h"

#define EC800_AT_LINE_BUF_SIZE ABOX_EC800_AT_LINE_SIZE
#define EC800_AT_CMD_BUF_SIZE ABOX_EC800_AT_COMMAND_SIZE
#define EC800_AT_QUEUE_SIZE ABOX_EC800_AT_QUEUE_SIZE
#define EC800_AT_RAW_CHUNK_MAX ABOX_EC800_AT_RAW_MAX

#define EC800_AT_OWNER_NONE ABOX_EC800_OWNER_NONE
#define EC800_AT_OWNER_MQTT ABOX_EC800_OWNER_MQTT
#define EC800_AT_OWNER_OTA ABOX_EC800_OWNER_OTA
#define EC800_AT_OWNER_NTRIP ABOX_EC800_OWNER_PRODUCT_BASE
#define EC800_AT_PRIORITY_NORMAL ABOX_EC800_PRIORITY_NORMAL
#define EC800_AT_PRIORITY_HIGH ABOX_EC800_PRIORITY_HIGH
#define EC800_AT_EVENT_LINE ABOX_EC800_EVENT_LINE
#define EC800_AT_EVENT_RAW ABOX_EC800_EVENT_RAW
#define EC800_AT_CMD_RESULT_OK ABOX_EC800_RESULT_OK
#define EC800_AT_CMD_RESULT_ERROR ABOX_EC800_RESULT_ERROR
#define EC800_AT_CMD_RESULT_TIMEOUT ABOX_EC800_RESULT_TIMEOUT
#define EC800_AT_CMD_RESULT_SEND_OK ABOX_EC800_RESULT_SEND_OK
#define EC800_AT_CMD_RESULT_SEND_FAIL ABOX_EC800_RESULT_SEND_FAIL

typedef ABoxEc800Owner Ec800AtOwner_t;
typedef ABoxEc800Priority Ec800AtPriority_t;
typedef ABoxEc800Event Ec800AtEvent_t;
typedef ABoxEc800Result Ec800AtCmdResult_t;
typedef ABoxEc800EventFn Ec800AtUrcHandler_t;
typedef ABoxEc800DoneFn Ec800AtCmdCallback_t;
typedef ABoxEc800Command Ec800AtCommand_t;
typedef ABoxEc800At Ec800AtCore_t;

uint8_t EC800_AT_Init(Ec800AtCore_t *core, const ABoxEc800AtPort *port);
void EC800_AT_Reset(Ec800AtCore_t *core);
void EC800_AT_FeedRx(Ec800AtCore_t *core, const uint8_t *data, uint16_t len);
void EC800_AT_Task(Ec800AtCore_t *core);
uint8_t EC800_AT_Submit(Ec800AtCore_t *core, const char *cmd,
                        Ec800AtOwner_t owner, Ec800AtPriority_t priority,
                        uint32_t timeout_ms, Ec800AtCmdCallback_t cb,
                        void *user);
uint8_t EC800_AT_SendPayload(Ec800AtCore_t *core, const uint8_t *data,
                             uint16_t len);
uint8_t EC800_AT_SendData(Ec800AtCore_t *core, const uint8_t *data,
                          uint16_t len);
uint8_t EC800_AT_SendPayloadForOwner(Ec800AtCore_t *core,
                                     Ec800AtOwner_t owner,
                                     const uint8_t *data, uint16_t len);
uint8_t EC800_AT_ForceSendPayloadForOwner(Ec800AtCore_t *core,
                                          Ec800AtOwner_t owner,
                                          const uint8_t *data, uint16_t len);
uint8_t EC800_AT_RegisterUrcHandler(Ec800AtCore_t *core, Ec800AtOwner_t owner,
                                 Ec800AtUrcHandler_t handler, void *user);
uint8_t EC800_AT_HasPending(Ec800AtCore_t *core, Ec800AtOwner_t owner);
uint8_t EC800_AT_IsBusy(Ec800AtCore_t *core);
uint8_t EC800_AT_IsActiveOwner(Ec800AtCore_t *core, Ec800AtOwner_t owner);
uint8_t EC800_AT_IsPayloadBusy(Ec800AtCore_t *core);
void EC800_AT_CancelQueuedOwner(Ec800AtCore_t *core, Ec800AtOwner_t owner);
void EC800_AT_CancelOwner(Ec800AtCore_t *core, Ec800AtOwner_t owner);
uint8_t EC800_AT_BeginRawRead(Ec800AtCore_t *core, Ec800AtOwner_t owner,
                              uint32_t length);
uint8_t EC800_AT_BeginRaw(Ec800AtCore_t *core, Ec800AtOwner_t owner,
                          uint32_t length);
void EC800_AT_CancelAll(Ec800AtCore_t *core);
void EC800_AT_AbortAll(Ec800AtCore_t *core, Ec800AtCmdResult_t result);
void EC800_AT_SetRxOverflowCount(Ec800AtCore_t *core, uint32_t count);
uint32_t EC800_AT_RxOverflowCount(const Ec800AtCore_t *core);
uint32_t EC800_AT_LineOverflowCount(const Ec800AtCore_t *core);

#define EC800_AT_Feed EC800_AT_FeedRx

#endif
