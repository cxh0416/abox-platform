#ifndef ABOX_NOR_H
#define ABOX_NOR_H

#include <stdint.h>

#define ABOX_NOR_CAPACITY 0x400000U
#define ABOX_NOR_SECTOR_SIZE 4096U
#define ABOX_NOR_PAGE_SIZE 256U
#define ABOX_NOR_OTA_SLOT_SIZE 0x40000U
#define ABOX_NOR_IDEM_OFFSET (2U * ABOX_NOR_OTA_SLOT_SIZE)
#ifndef ABOX_NOR_IDEM_KIB
#define ABOX_NOR_IDEM_KIB 1536U
#endif
#if ABOX_NOR_IDEM_KIB < 12U || ABOX_NOR_IDEM_KIB > (ABOX_NOR_CAPACITY - ABOX_NOR_IDEM_OFFSET) / 1024U || ABOX_NOR_IDEM_KIB % 4U
#error "ABOX_NOR_IDEM_KIB must be a 4 KiB multiple in the supported range"
#endif
#define ABOX_NOR_IDEM_SIZE (ABOX_NOR_IDEM_KIB * 1024U)
#define ABOX_NOR_UNASSIGNED_OFFSET (ABOX_NOR_IDEM_OFFSET + ABOX_NOR_IDEM_SIZE)
#define ABOX_NOR_UNASSIGNED_SIZE (ABOX_NOR_CAPACITY - ABOX_NOR_UNASSIGNED_OFFSET)

typedef enum {
    ABOX_NOR_OTA_SLOT0, ABOX_NOR_OTA_SLOT1, ABOX_NOR_IDEMPOTENCY
} ABoxNorPartition;

/* One serialized owner context (the App service task or Boot). Transfers
 * clock exactly length bytes with CS already asserted. Null TX clocks dummy
 * bytes, null RX discards the result. A failed transfer releases CS. */
typedef struct {
    void *context;
    void (*select)(void *context, int selected);
    int (*transfer)(void *context, const uint8_t *tx, uint8_t *rx, uint32_t length);
    uint32_t (*tick_ms)(void *context);
} ABoxNorSpi;

typedef struct ABoxNor {
    ABoxNorSpi spi;
    uint32_t erase_started;
    uint8_t ready, erasing, failed;
} ABoxNor;

typedef enum { ABOX_NOR_ERROR = -1, ABOX_NOR_BUSY = 0, ABOX_NOR_OK = 1 } ABoxNorResult;

/* During MCU-only reset the NOR may still be finishing an erase. Open waits
 * with a deadline before reading JEDEC ID (expected 0xC84016). */
int ABoxNor_Open(ABoxNor *nor, const ABoxNorSpi *spi, uint32_t *jedec_id);
ABoxNorResult ABoxNor_Status(ABoxNor *nor);
ABoxNorResult ABoxNor_Read(ABoxNor *nor, ABoxNorPartition partition,
    uint32_t offset, uint8_t *data, uint32_t length);
ABoxNorResult ABoxNor_Program(ABoxNor *nor, ABoxNorPartition partition,
    uint32_t offset, const uint8_t *data, uint32_t length);
/* Start one aligned 4 KiB erase. Poll Status before using the sector.
 * Program/Read return BUSY during an erase. Program waits per page only. */
ABoxNorResult ABoxNor_StartErase(ABoxNor *nor, ABoxNorPartition partition, uint32_t offset);

/* Reflected CRC-32, initial value zero. Pass the previous value to continue. */
uint32_t ABoxNor_Crc32(uint32_t crc, const uint8_t *data, uint32_t length);

#endif
