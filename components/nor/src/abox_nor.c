#include "abox_nor.h"
#include <string.h>

#define ERASE_TIMEOUT_MS 500U
#define PROGRAM_TIMEOUT_MS 10U
#define STATUS_BUSY 1U
#define STATUS_WEL 2U

static int command(ABoxNor *nor, const uint8_t *header, uint32_t header_length,
    const uint8_t *tx, uint8_t *rx, uint32_t length)
{
    ABoxNorSpi *bus = &nor->spi;
    bus->select(bus->context, 1);
    int ok = bus->transfer(bus->context, header, NULL, header_length);
    if (ok && length) ok = bus->transfer(bus->context, tx, rx, length);
    bus->select(bus->context, 0);
    return ok;
}

static int status(ABoxNor *nor, uint8_t *value)
{
    const uint8_t op = 0x05U;
    return command(nor, &op, 1U, NULL, value, 1U);
}

static uint32_t tick(ABoxNor *nor)
{
    return nor->spi.tick_ms(nor->spi.context);
}

static int wait_ready(ABoxNor *nor, uint32_t timeout)
{
    uint32_t started = tick(nor);
    uint8_t value;
    do {
        if (!status(nor, &value)) return 0;
        if (!(value & STATUS_BUSY)) return 1;
    } while ((uint32_t)(tick(nor) - started) < timeout);
    return 0;
}

int ABoxNor_Open(ABoxNor *nor, const ABoxNorSpi *spi, uint32_t *jedec_id)
{
    const uint8_t op = 0x9FU;
    uint8_t id[3];
    if (!nor || !spi || !spi->select || !spi->transfer || !spi->tick_ms) return 0;
    memset(nor, 0, sizeof(*nor));
    nor->spi = *spi;
    if (!wait_ready(nor, ERASE_TIMEOUT_MS) ||
        !command(nor, &op, 1U, NULL, id, sizeof(id))) return 0;
    uint32_t value = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    if (jedec_id) *jedec_id = value;
    nor->ready = value == 0xC84016U;
    return nor->ready;
}

ABoxNorResult ABoxNor_Status(ABoxNor *nor)
{
    if (!nor || !nor->ready || nor->failed) return ABOX_NOR_ERROR;
    if (!nor->erasing) return ABOX_NOR_OK;
    uint8_t value;
    if (status(nor, &value)) {
        if (!(value & STATUS_BUSY)) {
            nor->erasing = 0U;
            return ABOX_NOR_OK;
        }
        if ((uint32_t)(tick(nor) - nor->erase_started) < ERASE_TIMEOUT_MS) return ABOX_NOR_BUSY;
    }
    nor->failed = 1U;
    return ABOX_NOR_ERROR;
}

static int address_of(ABoxNorPartition part, uint32_t offset, uint32_t length, uint32_t *address)
{
    if ((unsigned)part > ABOX_NOR_IDEMPOTENCY || !length) return 0;
    uint32_t size = part == ABOX_NOR_IDEMPOTENCY ? ABOX_NOR_IDEM_SIZE : ABOX_NOR_OTA_SLOT_SIZE;
    if (offset >= size || length > size - offset) return 0;
    *address = (uint32_t)part * ABOX_NOR_OTA_SLOT_SIZE + offset;
    return 1;
}

static int addressed(ABoxNor *nor, uint8_t opcode, uint32_t address,
    const uint8_t *tx, uint8_t *rx, uint32_t length)
{
    const uint8_t header[] = {opcode, (uint8_t)(address >> 16),
                              (uint8_t)(address >> 8), (uint8_t)address};
    return command(nor, header, sizeof(header), tx, rx, length);
}

static int enable_write(ABoxNor *nor)
{
    const uint8_t op = 0x06U;
    uint8_t value;
    return command(nor, &op, 1U, NULL, NULL, 0U) && status(nor, &value) &&
        (value & STATUS_WEL) != 0U;
}

ABoxNorResult ABoxNor_Read(ABoxNor *nor, ABoxNorPartition part,
    uint32_t offset, uint8_t *data, uint32_t length)
{
    uint32_t address;
    if (!data || !address_of(part, offset, length, &address)) return ABOX_NOR_ERROR;
    /* After a mutation timeout, allow verified reads once the device can
     * report idle again. Keep mutations disabled until an explicit reopen. */
    if (nor && nor->failed) {
        uint8_t value;
        if (!status(nor, &value) || (value & STATUS_BUSY)) return ABOX_NOR_ERROR;
    } else {
        ABoxNorResult result = ABoxNor_Status(nor);
        if (result != ABOX_NOR_OK) return result;
    }
    return addressed(nor, 0x03U, address, NULL, data, length) ? ABOX_NOR_OK : ABOX_NOR_ERROR;
}

ABoxNorResult ABoxNor_Program(ABoxNor *nor, ABoxNorPartition part,
    uint32_t offset, const uint8_t *data, uint32_t length)
{
    uint32_t address;
    if (!data || !address_of(part, offset, length, &address)) return ABOX_NOR_ERROR;
    ABoxNorResult result = ABoxNor_Status(nor);
    if (result != ABOX_NOR_OK) return result;
    while (length) {
        uint32_t chunk = ABOX_NOR_PAGE_SIZE - address % ABOX_NOR_PAGE_SIZE;
        if (chunk > length) chunk = length;
        if (!enable_write(nor) || !addressed(nor, 0x02U, address, data, NULL, chunk) ||
            !wait_ready(nor, PROGRAM_TIMEOUT_MS)) {
            nor->failed = 1U;
            return ABOX_NOR_ERROR;
        }
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    return ABOX_NOR_OK;
}

ABoxNorResult ABoxNor_StartErase(ABoxNor *nor, ABoxNorPartition part, uint32_t offset)
{
    uint32_t address;
    if (offset % ABOX_NOR_SECTOR_SIZE ||
        !address_of(part, offset, ABOX_NOR_SECTOR_SIZE, &address)) return ABOX_NOR_ERROR;
    ABoxNorResult result = ABoxNor_Status(nor);
    if (result != ABOX_NOR_OK) return result;
    if (!enable_write(nor) || !addressed(nor, 0x20U, address, NULL, NULL, 0U)) {
        nor->failed = 1U;
        return ABOX_NOR_ERROR;
    }
    nor->erase_started = tick(nor);
    nor->erasing = 1U;
    return ABOX_NOR_OK;
}

uint32_t ABoxNor_Crc32(uint32_t crc, const uint8_t *data, uint32_t length)
{
    crc = ~crc;
    while (length--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
