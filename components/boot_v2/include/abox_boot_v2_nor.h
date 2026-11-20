#ifndef ABOX_BOOT_V2_NOR_H
#define ABOX_BOOT_V2_NOR_H

#include "abox_nor.h"

#define ABOX_BOOT_V2_NOR_HEADER_SIZE ABOX_NOR_PAGE_SIZE
#define ABOX_BOOT_V2_NOR_PAYLOAD_MAX (ABOX_NOR_OTA_SLOT_SIZE - ABOX_BOOT_V2_NOR_HEADER_SIZE)

typedef struct {
    ABoxNor *nor;
    uint32_t size, expected_crc, offset, erased, verified, crc;
    uint8_t slot, erase_pending, finished;
} ABoxBootV2NorWriter;

typedef struct {
    ABoxNor *nor;
    uint32_t size, crc, offset;
    uint8_t slot, opened;
} ABoxBootV2NorReader;

/* Call with the non-stable slot. Each erase step starts or polls one sector.
 * Program takes at most a page of source bytes. Verify reads successive
 * chunks, validates the vector/CRC again and commits slot metadata last. */
int ABoxBootV2NorWriter_Init(ABoxBootV2NorWriter *writer, ABoxNor *nor,
    uint8_t slot, uint32_t size, uint32_t crc);
ABoxNorResult ABoxBootV2NorWriter_EraseTask(ABoxBootV2NorWriter *writer);
ABoxNorResult ABoxBootV2NorWriter_Append(ABoxBootV2NorWriter *writer,
    const uint8_t *data, uint32_t length);
ABoxNorResult ABoxBootV2NorWriter_VerifyTask(ABoxBootV2NorWriter *writer,
    uint8_t *scratch, uint32_t capacity);

/* Metadata and first vector match the requested state record. The Boot
 * installer separately checks the entire payload before erasing MCU flash. */
int ABoxBootV2Nor_Matches(ABoxNor *nor, uint8_t slot, uint32_t size, uint32_t crc);
int ABoxBootV2NorReader_Open(void *context, uint8_t slot);
int ABoxBootV2NorReader_Read(void *context, uint8_t *data, uint32_t length);
int ABoxBootV2NorReader_Close(void *context);

#endif
