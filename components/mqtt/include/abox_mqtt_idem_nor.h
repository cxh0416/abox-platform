#ifndef ABOX_MQTT_IDEM_NOR_H
#define ABOX_MQTT_IDEM_NOR_H

#include "abox_mqtt_idempotency.h"
#include "abox_nor.h"

#define ABOX_IDEM_NOR_RESPONSE_MAX 1024U
#define ABOX_IDEM_NOR_SECTORS (ABOX_NOR_IDEM_SIZE / ABOX_NOR_SECTOR_SIZE)
/* Scale recovery space with the log. A small partition still needs one
 * layout sector, one intent sector and at least one log sector. */
#if ABOX_IDEM_NOR_SECTORS < 12U
#define ABOX_IDEM_NOR_RECLAIM_SECTORS 1U
#elif ABOX_IDEM_NOR_SECTORS >= 384U
#define ABOX_IDEM_NOR_RECLAIM_SECTORS 32U
#else
#define ABOX_IDEM_NOR_RECLAIM_SECTORS (ABOX_IDEM_NOR_SECTORS / 12U)
#endif
#define ABOX_IDEM_NOR_LOG_SECTORS (ABOX_IDEM_NOR_SECTORS - 1U - ABOX_IDEM_NOR_RECLAIM_SECTORS)

typedef struct {
    ABoxMqttIdempotency cache;
    ABoxNor *nor;
    uint32_t signature[ABOX_IDEM_NOR_LOG_SECTORS];
    uint32_t directory_crc[ABOX_IDEM_NOR_LOG_SECTORS];
    uint32_t protect_seconds[ABOX_IDEM_NOR_LOG_SECTORS];
    uint16_t end[ABOX_IDEM_NOR_LOG_SECTORS];
    uint8_t records[ABOX_IDEM_NOR_LOG_SECTORS];
    ABoxClockSnapshot reclaim_clock;
    uint64_t expires_at;
    uint32_t position;
    uint16_t cursor, next_sector, append_sector, victim, intent, written, final_length;
    uint8_t phase, state, reclaim_pending;
    uint8_t prepared[116];
    char pending_response[ABOX_IDEM_NOR_RESPONSE_MAX];
    char replay[ABOX_IDEM_NOR_RESPONSE_MAX + 1U];
    char device[33];
} ABoxMqttIdemNor;

/* Init before MQTT dispatch. Poll mounts incrementally and makes cache.ready
 * true after recovery. All calls use one serialized service task. Modem
 * reconnect/reset does not require reloading or invalidating this store.
 * A different on-flash partition layout leaves the cache unavailable. */
int ABoxMqttIdemNor_Init(ABoxMqttIdemNor *store, ABoxNor *nor, const char *device_id);
#endif
