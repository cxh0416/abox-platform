#include "abox_boot_v2_nor.h"
#include "abox_boot_v2.h"
#include <string.h>

#define SLOT_MAGIC 0x314E3242U
#define IMAGE_START ABOX_BOOT_V2_NOR_HEADER_SIZE

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

int ABoxBootV2Nor_Matches(ABoxNor *nor, uint8_t slot, uint32_t size, uint32_t crc)
{
    uint8_t header[16], vector[8];
    return slot <= 1U && size >= sizeof(vector) && size <= ABOX_BOOT_V2_NOR_PAYLOAD_MAX &&
        ABoxNor_Read(nor, (ABoxNorPartition)slot, 0U, header, sizeof(header)) == ABOX_NOR_OK &&
        get32(header) == SLOT_MAGIC && get32(header + 4U) == size && get32(header + 8U) == crc &&
        get32(header + 12U) == ABoxNor_Crc32(0, header + 4U, 8U) &&
        ABoxNor_Read(nor, (ABoxNorPartition)slot, IMAGE_START, vector, sizeof(vector)) == ABOX_NOR_OK &&
        ABoxBootV2_ImageVectorValid(vector);
}

int ABoxBootV2NorWriter_Init(ABoxBootV2NorWriter *w, ABoxNor *nor,
    uint8_t slot, uint32_t size, uint32_t crc)
{
    if (!w || !nor || slot > 1U || size < 8U || size > ABOX_BOOT_V2_NOR_PAYLOAD_MAX) return 0;
    memset(w, 0, sizeof(*w));
    w->nor = nor;
    w->slot = slot;
    w->size = size;
    w->expected_crc = crc;
    return 1;
}

ABoxNorResult ABoxBootV2NorWriter_EraseTask(ABoxBootV2NorWriter *w)
{
    if (!w || !w->nor || !w->size) return ABOX_NOR_ERROR;
    ABoxNorResult result = ABoxNor_Status(w->nor);
    if (result != ABOX_NOR_OK) return result;
    if (w->erase_pending) {
        /* Verify the header cannot still advertise an old committed image,
         * including when the device silently ignores erase protection. */
        if (!w->erased) {
            uint8_t header[16];
            if (ABoxNor_Read(w->nor, (ABoxNorPartition)w->slot,
                    0U, header, sizeof(header)) != ABOX_NOR_OK) return ABOX_NOR_ERROR;
            for (unsigned i = 0; i < sizeof(header); ++i)
                if (header[i] != 0xFFU) return ABOX_NOR_ERROR;
        }
        w->erased += ABOX_NOR_SECTOR_SIZE;
        w->erase_pending = 0U;
    }
    if (w->erased >= IMAGE_START + w->size) return ABOX_NOR_OK;
    result = ABoxNor_StartErase(w->nor, (ABoxNorPartition)w->slot, w->erased);
    if (result == ABOX_NOR_OK) w->erase_pending = 1U;
    return result == ABOX_NOR_ERROR ? result : ABOX_NOR_BUSY;
}

ABoxNorResult ABoxBootV2NorWriter_Append(ABoxBootV2NorWriter *w,
    const uint8_t *data, uint32_t length)
{
    if (!w || !w->nor || w->erased < IMAGE_START + w->size || !length ||
        length > ABOX_NOR_PAGE_SIZE || length > w->size - w->offset) return ABOX_NOR_ERROR;
    ABoxNorResult result = ABoxNor_Program(w->nor,
        (ABoxNorPartition)w->slot, IMAGE_START + w->offset, data, length);
    if (result == ABOX_NOR_OK) w->offset += length;
    return result;
}

ABoxNorResult ABoxBootV2NorWriter_VerifyTask(ABoxBootV2NorWriter *w,
    uint8_t *scratch, uint32_t capacity)
{
    if (!w || !w->nor || !scratch || capacity < 8U || !w->size ||
        w->offset != w->size) return ABOX_NOR_ERROR;
    if (w->finished) return ABOX_NOR_OK;
    ABoxNorResult result = ABoxNor_Status(w->nor);
    if (result != ABOX_NOR_OK) return result;
    ABoxNorPartition part = (ABoxNorPartition)w->slot;
    if (w->verified < w->size) {
        uint32_t length = w->size - w->verified;
        if (length > capacity) length = capacity;
        result = ABoxNor_Read(w->nor, part, IMAGE_START + w->verified, scratch, length);
        if (result != ABOX_NOR_OK) return result;
        if (!w->verified && !ABoxBootV2_ImageVectorValid(scratch)) return ABOX_NOR_ERROR;
        w->crc = ABoxNor_Crc32(w->crc, scratch, length);
        w->verified += length;
        return ABOX_NOR_BUSY;
    }
    if (w->crc != w->expected_crc) return ABOX_NOR_ERROR;

    uint8_t header[16], readback[12];
    put32(header, SLOT_MAGIC);
    put32(header + 4U, w->size);
    put32(header + 8U, w->crc);
    put32(header + 12U, ABoxNor_Crc32(0, header + 4U, 8U));
    if (ABoxNor_Program(w->nor, part, 4U, header + 4U, 12U) != ABOX_NOR_OK ||
        ABoxNor_Read(w->nor, part, 4U, readback, sizeof(readback)) != ABOX_NOR_OK ||
        memcmp(header + 4U, readback, sizeof(readback)) ||
        ABoxNor_Program(w->nor, part, 0U, header, 4U) != ABOX_NOR_OK ||
        !ABoxBootV2Nor_Matches(w->nor, w->slot, w->size, w->crc)) return ABOX_NOR_ERROR;
    w->finished = 1U;
    return ABOX_NOR_OK;
}

int ABoxBootV2NorReader_Open(void *context, uint8_t slot)
{
    ABoxBootV2NorReader *r = context;
    if (!r || !ABoxBootV2Nor_Matches(r->nor, slot, r->size, r->crc)) return 0;
    r->slot = slot;
    r->offset = 0U;
    r->opened = 1U;
    return 1;
}

int ABoxBootV2NorReader_Read(void *context, uint8_t *data, uint32_t length)
{
    ABoxBootV2NorReader *r = context;
    if (!r || !r->opened || length > r->size - r->offset ||
        ABoxNor_Read(r->nor, (ABoxNorPartition)r->slot,
                    IMAGE_START + r->offset, data, length) != ABOX_NOR_OK) return 0;
    r->offset += length;
    return 1;
}

int ABoxBootV2NorReader_Close(void *context)
{
    ABoxBootV2NorReader *r = context;
    if (!r) return 0;
    r->opened = 0U;
    return 1;
}
