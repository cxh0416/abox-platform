#include "abox_ec800_at_legacy.h"

uint8_t EC800_AT_Init(Ec800AtCore_t *core, const ABoxEc800AtPort *port)
{ return (uint8_t)ABoxEc800At_Init(core, port); }
void EC800_AT_Reset(Ec800AtCore_t *core) { ABoxEc800At_Reset(core); }
void EC800_AT_FeedRx(Ec800AtCore_t *core, const uint8_t *data, uint16_t len)
{ ABoxEc800At_Feed(core, data, len); }
void EC800_AT_Task(Ec800AtCore_t *core) { ABoxEc800At_Task(core); }
uint8_t EC800_AT_Submit(Ec800AtCore_t *core, const char *cmd,
                        Ec800AtOwner_t owner, Ec800AtPriority_t priority,
                        uint32_t timeout_ms, Ec800AtCmdCallback_t cb, void *user)
{ return (uint8_t)ABoxEc800At_Submit(core, cmd, owner, priority, timeout_ms, cb, user); }
uint8_t EC800_AT_SendPayload(Ec800AtCore_t *core, const uint8_t *data, uint16_t len)
{
    if (!core || !core->active_valid) return 0U;
    return (uint8_t)ABoxEc800At_SendPayload(core, core->active.owner, data, len);
}
uint8_t EC800_AT_SendData(Ec800AtCore_t *core, const uint8_t *data, uint16_t len)
{
    if (!core || !core->active_valid) return 0U;
    return (uint8_t)ABoxEc800At_ForcePayload(core, core->active.owner, data, len);
}
uint8_t EC800_AT_SendPayloadForOwner(Ec800AtCore_t *core,
                                     Ec800AtOwner_t owner,
                                     const uint8_t *data, uint16_t len)
{ return (uint8_t)ABoxEc800At_SendPayload(core, owner, data, len); }
uint8_t EC800_AT_ForceSendPayloadForOwner(Ec800AtCore_t *core,
                                          Ec800AtOwner_t owner,
                                          const uint8_t *data, uint16_t len)
{ return (uint8_t)ABoxEc800At_ForcePayload(core, owner, data, len); }
uint8_t EC800_AT_RegisterUrcHandler(Ec800AtCore_t *core, Ec800AtOwner_t owner,
                                 Ec800AtUrcHandler_t handler, void *user)
{ return (uint8_t)ABoxEc800At_Register(core, owner, handler, user); }
uint8_t EC800_AT_HasPending(Ec800AtCore_t *core, Ec800AtOwner_t owner)
{ return (uint8_t)ABoxEc800At_HasPending(core, owner); }
uint8_t EC800_AT_IsBusy(Ec800AtCore_t *core)
{ return (uint8_t)ABoxEc800At_IsBusy(core); }
uint8_t EC800_AT_IsActiveOwner(Ec800AtCore_t *core, Ec800AtOwner_t owner)
{ return (uint8_t)ABoxEc800At_IsActive(core, owner); }
uint8_t EC800_AT_IsPayloadBusy(Ec800AtCore_t *core)
{ return (uint8_t)ABoxEc800At_IsPayloadBusy(core); }
void EC800_AT_CancelQueuedOwner(Ec800AtCore_t *core, Ec800AtOwner_t owner)
{ ABoxEc800At_CancelQueued(core, owner); }
void EC800_AT_CancelOwner(Ec800AtCore_t *core, Ec800AtOwner_t owner)
{ ABoxEc800At_Cancel(core, owner); }
uint8_t EC800_AT_BeginRawRead(Ec800AtCore_t *core, Ec800AtOwner_t owner,
                              uint32_t length)
{ return (uint8_t)ABoxEc800At_BeginRaw(core, owner, length); }
uint8_t EC800_AT_BeginRaw(Ec800AtCore_t *core, Ec800AtOwner_t owner,
                          uint32_t length)
{ return (uint8_t)ABoxEc800At_BeginRaw(core, owner, length); }
void EC800_AT_CancelAll(Ec800AtCore_t *core) { ABoxEc800At_CancelAll(core); }
void EC800_AT_AbortAll(Ec800AtCore_t *core, Ec800AtCmdResult_t result)
{ ABoxEc800At_AbortAll(core, result); }
void EC800_AT_SetRxOverflowCount(Ec800AtCore_t *core, uint32_t count)
{ ABoxEc800At_SetRxOverflowCount(core, count); }
uint32_t EC800_AT_RxOverflowCount(const Ec800AtCore_t *core)
{ return ABoxEc800At_RxOverflowCount(core); }
uint32_t EC800_AT_LineOverflowCount(const Ec800AtCore_t *core)
{ return ABoxEc800At_LineOverflowCount(core); }
