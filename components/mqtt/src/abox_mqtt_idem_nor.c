#include "abox_mqtt_idem_nor.h"
#include "abox_mqtt_request.h"
#include <string.h>

#define SECTOR_SIZE ABOX_NOR_SECTOR_SIZE
#define LOG_COUNT ABOX_IDEM_NOR_LOG_SECTORS
#define INTENT_COUNT ABOX_IDEM_NOR_RECLAIM_SECTORS
#define MAX_RESPONSE ABOX_IDEM_NOR_RESPONSE_MAX
#define LAYOUT_MAGIC 0x314C4D49U
#define SEGMENT_MAGIC 0x31474D49U
#define PREPARED_MAGIC 0x31504D49U
#define FINAL_MAGIC 0x31464D49U
#define INTENT_MAGIC 0x31454D49U
#define LAYOUT_SIZE 16U
#define SEGMENT_SIZE 44U
#define FINAL_SIZE 16U
#define INTENT_SIZE 16U
#define PREPARED_SIZE 116U
#define PREPARED_ID 44U
#define PREPARED_DIGEST 12U
#define RECORD_FIXED (PREPARED_SIZE + FINAL_SIZE)
#define DIRECTORY_ENTRIES 30U
#define DATA_END (SECTOR_SIZE - DIRECTORY_ENTRIES * 4U)
#define MAX_RESERVATION (RECORD_FIXED + MAX_RESPONSE)
#define ENTRY_OFFSET_BITS 10U
#define ENTRY_OFFSET_MASK ((1U << ENTRY_OFFSET_BITS) - 1U)
#define ENTRY_HASH_MASK (~ENTRY_OFFSET_MASK)
#define NO_SECTOR UINT16_MAX

_Static_assert(DIRECTORY_ENTRIES >= (DATA_END - SEGMENT_SIZE) / (RECORD_FIXED + 4U),
               "Directory fits the maximum number of records");

enum {
    MOUNT_LAYOUT, MOUNT_INTENT, MOUNT_LOG, RUN,
    ERASE_LOG, WAIT_LOG, ERASE_INTENT, WAIT_INTENT
};

typedef struct {
    uint32_t response_crc, next;
    uint16_t length;
    uint8_t final;
} Record;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static uint64_t get_expiry(const uint8_t *p)
{
    return ((uint64_t)get32(p + 8U) << 32) | get32(p + 4U);
}

static int blank(const uint8_t *p, uint32_t length)
{
    while (length--) if (*p++ != 0xFFU) return 0;
    return 1;
}

static uint32_t base(unsigned sector)
{
    return (sector + 1U) * SECTOR_SIZE;
}

static uint32_t hash_id(const char *id)
{
    uint32_t hash = 2166136261U;
    for (; *id; ++id) hash = (hash ^ (uint8_t)*id) * 16777619U;
    return hash ^ (hash >> 16);
}

static uint32_t signature(uint32_t hash)
{
    return (1UL << (hash & 31U)) | (1UL << ((hash >> 16) & 31U));
}

static uint32_t directory_entry(const char *id, uint32_t offset)
{
    return (hash_id(id) & ENTRY_HASH_MASK) | ((offset % SECTOR_SIZE) / 4U);
}

static int read_at(ABoxMqttIdemNor *s, uint32_t offset, uint8_t *p, uint32_t length)
{
    return ABoxNor_Read(s->nor, ABOX_NOR_IDEMPOTENCY, offset, p, length) == ABOX_NOR_OK;
}

static int program_verified(ABoxMqttIdemNor *s, uint32_t offset,
    const uint8_t *p, uint32_t length)
{
    uint8_t verify[ABOX_NOR_PAGE_SIZE];
    return length <= sizeof(verify) &&
        ABoxNor_Program(s->nor, ABOX_NOR_IDEMPOTENCY, offset, p, length) == ABOX_NOR_OK &&
        read_at(s, offset, verify, length) && !memcmp(verify, p, length);
}

/* Body and CRC reach flash and are checked before the marker is written. */
static int commit(ABoxMqttIdemNor *s, uint32_t offset, uint8_t *p, uint32_t size, uint32_t magic)
{
    put32(p + size - 4U, ABoxNor_Crc32(0, p + 4U, size - 8U));
    put32(p, magic);
    return program_verified(s, offset + 4U, p + 4U, size - 4U) &&
        program_verified(s, offset, p, 4U);
}

static int valid_crc(const uint8_t *p, uint32_t size)
{
    return get32(p + size - 4U) == ABoxNor_Crc32(0, p + 4U, size - 8U);
}

static int compatible_marker(uint32_t marker, uint32_t magic)
{
    /* A torn program can leave expected zero bits at one. */
    return (marker & magic) == magic;
}

/* The first sector stays outside reclamation and ties the live journal to
 * the configured boundary. An incompatible build must not scan or erase it. */
static int mount_layout(ABoxMqttIdemNor *s)
{
    uint8_t actual[LAYOUT_SIZE], expected[LAYOUT_SIZE];
    put32(expected, LAYOUT_MAGIC);
    put32(expected + 4U, ABOX_NOR_IDEM_SIZE);
    put32(expected + 8U, ~ABOX_NOR_IDEM_SIZE);
    put32(expected + 12U, ABoxNor_Crc32(0, expected + 4U, 8U));
    if (!read_at(s, 0U, actual, sizeof(actual))) return 0;
    if (!memcmp(actual, expected, sizeof(actual))) return 1;
    if (get32(actual) == LAYOUT_MAGIC) return 0;

    /* Resume first-use initialization after a torn program of this header. */
    for (unsigned i = 0; i < sizeof(actual); ++i)
        if ((actual[i] & expected[i]) != expected[i]) return 0;
    return commit(s, 0U, expected, sizeof(expected), LAYOUT_MAGIC);
}

static int segment_header(ABoxMqttIdemNor *s, unsigned sector, uint8_t p[SEGMENT_SIZE])
{
    if (!read_at(s, base(sector), p, SEGMENT_SIZE)) return -1;
    uint32_t marker = get32(p);
    if (marker != SEGMENT_MAGIC)
        return blank(p, SEGMENT_SIZE) ? 0 : (compatible_marker(marker, SEGMENT_MAGIC) ? 1 : -1);
    if (!valid_crc(p, SEGMENT_SIZE) || !memchr(p + 4U, 0, 33U) ||
        !ABoxMqttV4_IdValid((const char *)p + 4U, ABOX_MQTT_ID_DEVICE)) return -1;
    return 2;
}

/* -1 corrupt or unreadable, 0 erased tail, 1 torn preparation, 2 prepared.
 * A prepared record always reserves a final header; an incomplete final
 * seals that segment. Subsequent appends never follow an incomplete final. */
static int read_record(ABoxMqttIdemNor *s, uint32_t offset, uint8_t *prep, Record *record)
{
    uint8_t final[FINAL_SIZE];
    if (offset % SECTOR_SIZE >= DATA_END) return 0;
    uint32_t remaining = DATA_END - offset % SECTOR_SIZE;
    if (remaining < RECORD_FIXED + 4U) return 0;
    if (!read_at(s, offset, prep, PREPARED_SIZE)) return -1;
    uint32_t marker = get32(prep);
    if (marker != PREPARED_MAGIC) {
        if (!compatible_marker(marker, PREPARED_MAGIC)) return -1;
        return blank(prep, PREPARED_SIZE) ? 0 : 1;
    }
    if (!valid_crc(prep, PREPARED_SIZE) || !get_expiry(prep) ||
        get_expiry(prep) > 9007199254740991ULL ||
        !memchr(prep + PREPARED_ID, 0, 65U) ||
        !ABoxMqttV4_IdValid((const char *)prep + PREPARED_ID, ABOX_MQTT_ID_REQUEST) ||
        !read_at(s, offset + PREPARED_SIZE, final, sizeof(final))) return -1;
    record->next = offset - offset % SECTOR_SIZE + DATA_END;
    record->response_crc = 0U;
    record->length = 0U;
    record->final = 0U;
    marker = get32(final);
    if (marker != FINAL_MAGIC) return compatible_marker(marker, FINAL_MAGIC) ? 2 : -1;
    uint32_t length = get32(final + 4U);
    if (!valid_crc(final, FINAL_SIZE) || !length || length > MAX_RESPONSE ||
        length > remaining - RECORD_FIXED) return -1;
    record->final = 1U;
    record->length = (uint16_t)length;
    record->response_crc = get32(final + 8U);
    record->next = offset + RECORD_FIXED + ((length + 3U) & ~3U);
    return 2;
}

static void invalidate(void *context)
{
    ABoxMqttIdemNor *s = context;
    s->cache.failed = 1U;
    if (s->state != ABOX_IDEM_EMPTY && s->state != ABOX_IDEM_FINAL)
        s->state = ABOX_IDEM_INTERRUPTED;
}

static int mount_segment(ABoxMqttIdemNor *s, unsigned sector)
{
    uint8_t p[PREPARED_SIZE], directory[DIRECTORY_ENTRIES * 4U];
    Record r;
    int result = segment_header(s, sector, p);
    if (!result) return 1;
    s->end[sector] = DATA_END;
    if (result < 0) return 0;
    if (result == 1) return 1;
    if (!read_at(s, base(sector) + DATA_END, directory, sizeof(directory))) return 0;

    int ours = !strcmp((const char *)p + 4U, s->device);
    uint32_t position = base(sector) + SEGMENT_SIZE;
    for (unsigned i = 0; i < DIRECTORY_ENTRIES; ++i) {
        int status = read_record(s, position, p, &r);
        uint32_t tag = get32(directory + i * 4U);
        if (status < 0) return 0;
        if (status == 0 && tag == UINT32_MAX) {
            s->end[sector] = (uint16_t)(position % SECTOR_SIZE);
            if (ours && s->end[sector] <= DATA_END - MAX_RESERVATION)
                s->append_sector = (uint16_t)sector;
            break;
        }
        if (status < 2) break;
        const char *id = (const char *)p + PREPARED_ID;
        if (tag != directory_entry(id, position)) return 0;
        if (ours) {
            s->signature[sector] |= signature(hash_id(id));
            s->directory_crc[sector] = ABoxNor_Crc32(s->directory_crc[sector], directory + i * 4U, 4U);
            s->records[sector]++;
        }
        if (!r.final) break;
        position = r.next;
    }
    return 1;
}

static int read_response(ABoxMqttIdemNor *s, uint32_t offset, const Record *r)
{
    if (!read_at(s, offset + RECORD_FIXED, (uint8_t *)s->replay, r->length) ||
        ABoxNor_Crc32(0, (const uint8_t *)s->replay, r->length) != r->response_crc ||
        memchr(s->replay, 0, r->length)) {
        if (ABoxNor_Status(s->nor) == ABOX_NOR_BUSY) return 0;
        invalidate(s);
        return -1;
    }
    s->replay[r->length] = 0;
    return 1;
}

static ABoxMqttIdemMatch find(void *context, const char *id, const uint8_t *digest, size_t *index)
{
    ABoxMqttIdemNor *s = context;
    if (!s->cache.ready || !id || !digest) return ABOX_IDEM_UNAVAILABLE;
    if (s->state != ABOX_IDEM_EMPTY && s->state != ABOX_IDEM_FINAL &&
        !strcmp(id, (const char *)s->prepared + PREPARED_ID)) {
        if (index) *index = s->position;
        if (memcmp(digest, s->prepared + PREPARED_DIGEST, 32U)) return ABOX_IDEM_CONFLICT;
        return s->state == ABOX_IDEM_INTERRUPTED ? ABOX_IDEM_UNCERTAIN : ABOX_IDEM_WAIT;
    }
    const uint32_t hash = hash_id(id);
    const uint32_t mask = signature(hash);
    for (unsigned i = 0; i < LOG_COUNT; ++i) {
        if ((s->signature[i] & mask) != mask) continue;
        uint8_t directory[DIRECTORY_ENTRIES * 4U];
        if (!read_at(s, base(i) + DATA_END, directory, s->records[i] * 4U)) {
            if (ABoxNor_Status(s->nor) == ABOX_NOR_BUSY) return ABOX_IDEM_UNAVAILABLE;
            invalidate(s);
            continue;
        }
        if (ABoxNor_Crc32(0, directory, s->records[i] * 4U) != s->directory_crc[i]) {
            invalidate(s);
            continue;
        }
        for (unsigned j = 0; j < s->records[i]; ++j) {
            uint32_t tag = get32(directory + j * 4U);
            if ((tag & ENTRY_HASH_MASK) != (hash & ENTRY_HASH_MASK)) continue;
            uint32_t offset = (tag & ENTRY_OFFSET_MASK) * 4U;
            if (s->state != ABOX_IDEM_FINAL && s->position == base(i) + offset) continue;
            uint8_t p[PREPARED_SIZE];
            Record r;
            if (offset < SEGMENT_SIZE || offset >= s->end[i] ||
                read_record(s, base(i) + offset, p, &r) != 2) {
                invalidate(s);
                continue;
            }
            if (strcmp(id, (const char *)p + PREPARED_ID)) continue;
            if (index) *index = base(i) + offset;
            if (memcmp(digest, p + PREPARED_DIGEST, 32U)) return ABOX_IDEM_CONFLICT;
            if (!r.final) return ABOX_IDEM_UNCERTAIN;
            int verified = read_response(s, base(i) + offset, &r);
            if (!verified) return ABOX_IDEM_UNAVAILABLE;
            return verified > 0 ? ABOX_IDEM_REPLAY : ABOX_IDEM_UNCERTAIN;
        }
    }
    return s->cache.failed ? ABOX_IDEM_UNAVAILABLE : ABOX_IDEM_NEW;
}

static uint32_t mono_protect(uint64_t expires, const ABoxClockSnapshot *clock)
{
    uint64_t until = ABoxMqttV4_IdempotencyProtectUntil(expires);
    uint64_t lower = clock->utc_ms - clock->error_ms;
    uint64_t remaining = until > lower ? until - lower : 0;
    if (clock->mono_ms >= (uint64_t)UINT32_MAX * 1000U ||
        remaining >= (uint64_t)UINT32_MAX * 1000U - clock->mono_ms) return UINT32_MAX;
    return (uint32_t)((clock->mono_ms + remaining + 999U) / 1000U);
}

static uint16_t begin(void *context, const char *id, const uint8_t *digest, uint64_t expires,
    const ABoxClockSnapshot *clock, size_t *index)
{
    ABoxMqttIdemNor *s = context;
    if (s->phase != RUN || s->cache.failed || ABoxNor_Status(s->nor) != ABOX_NOR_OK ||
        (s->state != ABOX_IDEM_EMPTY && s->state != ABOX_IDEM_FINAL)) return 4003U;
    uint16_t code = ABoxMqttV4_CheckExpiry(clock, expires);
    if (code != 200U) return code;
    if (clock->utc_ms < clock->error_ms) return 4003U;

    unsigned sector = s->append_sector;
    if (sector == NO_SECTOR || s->end[sector] > DATA_END - MAX_RESERVATION) {
        for (unsigned i = 0; i < LOG_COUNT; ++i) {
            sector = (s->next_sector + i) % LOG_COUNT;
            if (!s->end[sector]) break;
            sector = NO_SECTOR;
        }
    }
    if (sector == NO_SECTOR) {
        if (!s->reclaim_pending && clock->mono_ms >= s->reclaim_clock.mono_ms &&
            (s->reclaim_clock.quality == ABOX_CLOCK_INVALID ||
             clock->mono_ms - s->reclaim_clock.mono_ms >= 1000U)) {
            s->reclaim_clock = *clock;
            s->reclaim_pending = 1U;
            s->cursor = 0U;
        }
        return 4003U;
    }
    s->reclaim_pending = 0U;
    s->append_sector = (uint16_t)sector;
    uint32_t offset = s->end[sector] ? s->end[sector] : SEGMENT_SIZE;
    s->position = base(sector) + offset;
    s->expires_at = expires;
    memset(s->prepared, 0, sizeof(s->prepared));
    put32(s->prepared + 4U, (uint32_t)expires);
    put32(s->prepared + 8U, (uint32_t)(expires >> 32));
    memcpy(s->prepared + PREPARED_DIGEST, digest, 32U);
    strcpy((char *)s->prepared + PREPARED_ID, id);
    uint32_t deadline = mono_protect(expires, clock);
    if (s->protect_seconds[sector] < deadline) s->protect_seconds[sector] = deadline;
    s->state = ABOX_IDEM_PREPARING;
    *index = s->position;
    return 200U;
}

static int erasable(ABoxMqttIdemNor *s, unsigned sector)
{
    uint8_t p[PREPARED_SIZE];
    const ABoxClockSnapshot *clock = &s->reclaim_clock;
    uint64_t lower = clock->utc_ms - clock->error_ms;
    if (s->protect_seconds[sector] == UINT32_MAX ||
        clock->mono_ms / 1000U < s->protect_seconds[sector]) return 0;
    int status = segment_header(s, sector, p);
    if (status < 2) return status;
    uint32_t end = base(sector) + s->end[sector];
    for (uint32_t offset = base(sector) + SEGMENT_SIZE; offset < end;) {
        Record r;
        status = read_record(s, offset, p, &r);
        if (status < 2) return status >= 0 ? 1 : -1;
        if (lower <= ABoxMqttV4_IdempotencyProtectUntil(get_expiry(p))) return 0;
        offset = r.next;
    }
    return 1;
}

static int verify_erased(ABoxMqttIdemNor *s, uint32_t offset, uint32_t length)
{
    uint8_t p[ABOX_NOR_PAGE_SIZE];
    for (uint32_t i = 0; i < length; i += sizeof(p)) {
        uint32_t count = length - i < sizeof(p) ? length - i : sizeof(p);
        if (!read_at(s, offset + i, p, count) || !blank(p, count)) return 0;
    }
    return 1;
}

static int reclaim(ABoxMqttIdemNor *s)
{
    uint8_t p[INTENT_SIZE] = {0};
    unsigned sector = (s->next_sector + s->cursor++) % LOG_COUNT;
    if (s->cursor == LOG_COUNT) s->reclaim_pending = 0U;
    int eligible = erasable(s, sector);
    if (eligible <= 0) return eligible == 0;
    s->victim = (uint16_t)sector;
    s->intent = (uint16_t)(LOG_COUNT + sector % INTENT_COUNT);
    put32(p + 4U, sector);
    /* During torn erase, setting bits cannot change an intact value and
     * its complement into a different valid victim. */
    put32(p + 8U, ~(uint32_t)sector);
    if (!commit(s, base(s->intent), p, sizeof(p), INTENT_MAGIC)) return 0;
    s->phase = ERASE_LOG;
    s->reclaim_pending = 0U;
    return 1;
}

static int mount_intent(ABoxMqttIdemNor *s)
{
    if (s->cursor == INTENT_COUNT) {
        s->cursor = 0U;
        s->phase = MOUNT_LOG;
        return 1;
    }
    uint8_t p[INTENT_SIZE];
    s->intent = (uint16_t)(LOG_COUNT + s->cursor++);
    if (!read_at(s, base(s->intent), p, sizeof(p))) return 0;
    if (blank(p, sizeof(p))) return 1;
    uint32_t victim = get32(p + 4U);
    if (get32(p) == INTENT_MAGIC && get32(p + 8U) == ~victim && valid_crc(p, sizeof(p)) &&
        victim < LOG_COUNT && LOG_COUNT + victim % INTENT_COUNT == s->intent) {
        s->victim = (uint16_t)victim;
        s->phase = ERASE_LOG;
    } else {
        /* A torn intent write precedes erase. A torn intent cleanup follows
         * verified erase. In both cases the intent sector alone can be erased. */
        s->phase = ERASE_INTENT;
    }
    return 1;
}

static int progress_erase(ABoxMqttIdemNor *s)
{
    switch (s->phase) {
    case ERASE_LOG:
        if (ABoxNor_StartErase(s->nor, ABOX_NOR_IDEMPOTENCY, base(s->victim)) != ABOX_NOR_OK) return 0;
        s->phase = WAIT_LOG;
        break;
    case WAIT_LOG:
        if (!verify_erased(s, base(s->victim), SECTOR_SIZE)) return 0;
        s->end[s->victim] = s->records[s->victim] = 0U;
        s->signature[s->victim] = s->protect_seconds[s->victim] = s->directory_crc[s->victim] = 0U;
        if (s->append_sector == s->victim) s->append_sector = NO_SECTOR;
        s->next_sector = (uint16_t)((s->victim + 1U) % LOG_COUNT);
        s->phase = ERASE_INTENT;
        break;
    case ERASE_INTENT:
        if (ABoxNor_StartErase(s->nor, ABOX_NOR_IDEMPOTENCY, base(s->intent)) != ABOX_NOR_OK) return 0;
        s->phase = WAIT_INTENT;
        break;
    case WAIT_INTENT:
        if (!verify_erased(s, base(s->intent), INTENT_SIZE)) return 0;
        /* The reclaimed sector becomes available only after intent cleanup. */
        s->phase = s->cache.ready ? RUN : MOUNT_INTENT;
        break;
    default:
        return 0;
    }
    return 1;
}

static int prepare(ABoxMqttIdemNor *s)
{
    unsigned sector = s->append_sector;
    if (!s->end[sector]) {
        uint8_t header[SEGMENT_SIZE] = {0};
        memcpy(header + 4U, s->device, sizeof(s->device));
        if (!commit(s, base(sector), header, sizeof(header), SEGMENT_MAGIC)) return 0;
        s->end[sector] = SEGMENT_SIZE;
        s->next_sector = (uint16_t)((sector + 1U) % LOG_COUNT);
        return 1;
    }
    const char *id = (const char *)s->prepared + PREPARED_ID;
    uint8_t tag[4];
    put32(tag, directory_entry(id, s->position));
    uint32_t directory_offset = base(sector) + DATA_END + s->records[sector] * 4U;
    if (s->records[sector] >= DIRECTORY_ENTRIES ||
        !program_verified(s, directory_offset, tag, sizeof(tag)) ||
        !commit(s, s->position, s->prepared, sizeof(s->prepared), PREPARED_MAGIC)) return 0;
    s->directory_crc[sector] = ABoxNor_Crc32(s->directory_crc[sector], tag, sizeof(tag));
    s->records[sector]++;
    s->signature[sector] |= signature(hash_id(id));
    s->state = ABOX_IDEM_PREPARED;
    return 1;
}

static int finalize(ABoxMqttIdemNor *s)
{
    uint32_t offset = s->position + RECORD_FIXED + s->written;
    if (s->written < s->final_length) {
        uint32_t length = ABOX_NOR_PAGE_SIZE - offset % ABOX_NOR_PAGE_SIZE;
        if (length > (uint32_t)(s->final_length - s->written)) length = s->final_length - s->written;
        if (!program_verified(s, offset, (const uint8_t *)s->pending_response + s->written, length)) return 0;
        s->written += (uint16_t)length;
        return 1;
    }
    uint8_t header[FINAL_SIZE];
    put32(header + 4U, s->final_length);
    put32(header + 8U, ABoxNor_Crc32(0, (const uint8_t *)s->pending_response, s->final_length));
    if (!commit(s, s->position + PREPARED_SIZE, header, sizeof(header), FINAL_MAGIC)) return 0;
    s->end[s->append_sector] = (uint16_t)(((offset + 3U) & ~3U) - base(s->append_sector));
    s->state = ABOX_IDEM_FINAL;
    return 1;
}

static void poll(void *context)
{
    ABoxMqttIdemNor *s = context;
    ABoxNorResult status = ABoxNor_Status(s->nor);
    if (status == ABOX_NOR_BUSY) return;
    if (s->phase == MOUNT_LAYOUT) {
        if (!s->cache.failed) {
            if (mount_layout(s)) s->phase = MOUNT_INTENT;
            else invalidate(s);
        }
        return;
    }
    if (status == ABOX_NOR_ERROR) {
        invalidate(s);
        if (!s->cache.ready && s->phase != MOUNT_LOG) {
            s->cursor = 0U;
            s->phase = MOUNT_LOG;
        }
        if (s->cache.ready) return;
    }
    int ok = 1;
    if (s->phase == MOUNT_LOG) {
        ok = mount_segment(s, s->cursor++);
        if (s->cursor == LOG_COUNT) {
            s->phase = RUN;
            s->cache.ready = 1U;
        }
    } else if (s->cache.failed) {
        return;
    } else if (s->phase == MOUNT_INTENT) {
        ok = mount_intent(s);
        /* An unreadable intent prevents safe recovery. Still index the
         * readable log for replay, with new actions disabled. */
        if (!ok) { s->cursor = 0U; s->phase = MOUNT_LOG; }
    } else if (s->phase != RUN) {
        ok = progress_erase(s);
    } else if (s->state == ABOX_IDEM_PREPARING) {
        ok = prepare(s);
    } else if (s->state == ABOX_IDEM_FINALIZING) {
        ok = finalize(s);
    } else if (s->reclaim_pending) {
        ok = reclaim(s);
    }
    if (!ok) {
        invalidate(s);
        if (!s->cache.ready && s->phase != MOUNT_LOG && s->phase != RUN) {
            s->cursor = 0U;
            s->phase = MOUNT_LOG;
        }
    }
}

static uint16_t take(void *context, size_t index, const ABoxClockSnapshot *clock)
{
    ABoxMqttIdemNor *s = context;
    if (s->cache.failed || s->position != index || s->state != ABOX_IDEM_PREPARED) return 4003U;
    uint16_t code = ABoxMqttV4_CheckExpiry(clock, s->expires_at);
    if (code == 200U) s->state = ABOX_IDEM_DISPATCHED;
    return code;
}

static int complete(void *context, size_t index, const char *response, size_t length)
{
    ABoxMqttIdemNor *s = context;
    if (s->cache.failed || s->position != index ||
        (s->state != ABOX_IDEM_PREPARED && s->state != ABOX_IDEM_DISPATCHED) ||
        !response || !length || length > MAX_RESPONSE || memchr(response, 0, length)) return 0;
    memcpy(s->pending_response, response, length);
    s->final_length = (uint16_t)length;
    s->state = ABOX_IDEM_FINALIZING;
    s->written = 0U;
    return 1;
}

static int indexed_record(ABoxMqttIdemNor *s, size_t index, Record *r)
{
    uint8_t p[PREPARED_SIZE];
    if (index < SECTOR_SIZE) return 0;
    size_t sector = index / SECTOR_SIZE - 1U;
    if (!s->cache.ready || sector >= LOG_COUNT ||
        index % SECTOR_SIZE < SEGMENT_SIZE || index % SECTOR_SIZE >= s->end[sector] ||
        !s->signature[sector]) return 0;
    return read_record(s, (uint32_t)index, p, r) == 2;
}

static const char *response(void *context, size_t index, size_t *length)
{
    ABoxMqttIdemNor *s = context;
    Record r;
    if (!indexed_record(s, index, &r) || !r.final ||
        read_response(s, (uint32_t)index, &r) != 1) return NULL;
    if (length) *length = r.length;
    return s->replay;
}

static ABoxMqttIdemState state(void *context, size_t index)
{
    ABoxMqttIdemNor *s = context;
    Record r;
    if (s->state && s->position == index) return (ABoxMqttIdemState)s->state;
    if (!indexed_record(s, index, &r)) return ABOX_IDEM_EMPTY;
    return r.final ? ABOX_IDEM_FINAL : ABOX_IDEM_INTERRUPTED;
}

int ABoxMqttIdemNor_Init(ABoxMqttIdemNor *s, ABoxNor *nor, const char *device_id)
{
    static const ABoxMqttIdemBackend backend = {
        find, begin, poll, take, complete, response, invalidate, state
    };
    if (!s || !nor || !nor->ready || !ABoxMqttV4_IdValid(device_id, ABOX_MQTT_ID_DEVICE)) return 0;
    memset(s, 0, sizeof(*s));
    strcpy(s->device, device_id);
    s->nor = nor;
    s->append_sector = NO_SECTOR;
    s->cache.backend = &backend;
    s->cache.backend_context = s;
    s->cache.device_id = s->device;
    s->cache.response_capacity = MAX_RESPONSE + 1U;
    return 1;
}
