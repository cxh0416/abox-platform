#ifndef ABOX_EC800_ASYNC_H
#define ABOX_EC800_ASYNC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Serialized event-loop API. Adapters own globally unique operation IDs and
 * must drain late modem responses before accepting the next transaction.
 * poll returns PENDING, OK, or ERROR; cancel returns 1 only after quiescence.
 * No callbacks, HAL, RTOS, or product dependencies. */
typedef enum {
    ABOX_ASYNC_IDLE = 0, ABOX_ASYNC_PENDING, ABOX_ASYNC_OK,
    ABOX_ASYNC_ERROR, ABOX_ASYNC_TIMEOUT, ABOX_ASYNC_CANCELLED,
    ABOX_ASYNC_QUARANTINED,
    ABOX_ASYNC_NOT_FOUND, /* Exact file absence, never generic open/I/O failure. */
    ABOX_ASYNC_TRUNCATED /* Known EOF, file handle closed; not an I/O timeout. */
} ABoxAsyncStatus;
typedef struct {
    void *context;
    int (*submit)(void *context, const char *command, uint32_t timeout_ms,
                  uint64_t *operation);
    ABoxAsyncStatus (*poll)(void *context, uint64_t operation);
    int (*cancel)(void *context, uint64_t operation);
} ABoxEc800CommandPort;
#ifdef __cplusplus
}
#endif
#endif
