#include "abox_mqtt_idem_nor.h"
#undef NDEBUG
#include "nor_test_chip.h"
#include <stdio.h>
#include <time.h>

static NorTestChip chip;
static ABoxNor nor;
static ABoxMqttIdemNor store;
static uint8_t snapshot[ABOX_NOR_CAPACITY];
static char full_response[1024];
static unsigned effects;
static const uint8_t digest[32] = {7U}, conflict[32] = {8U};
static const ABoxClockSnapshot initial = {
    .utc_ms = 1800000000000ULL, .mono_ms = 1000, .error_ms = 5,
    .source = ABOX_CLOCK_PLATFORM, .quality = ABOX_CLOCK_SYNCHRONIZED
};
static ABoxClockSnapshot clock_now;

static void poll(unsigned count)
{
    while (count--) ABoxMqttIdem_Poll(&store.cache);
}

static void mount(const char *device)
{
    nor_test_open(&chip, &nor);
    assert(ABoxMqttIdemNor_Init(&store, &nor, device));
    for (unsigned i = 0; i < 1100 && !store.cache.ready; ++i) poll(1);
    assert(store.cache.ready && !store.cache.failed);
}

static void fresh(void)
{
    nor_test_fresh(&chip, &nor);
    effects = 0;
    clock_now = initial;
    mount("device-1");
}

static uint16_t begin(const char *id, uint32_t ttl, size_t *handle)
{
    return ABoxMqttIdem_Begin(&store.cache, id, digest, clock_now.utc_ms + ttl, &clock_now, handle);
}

static void dispatch(size_t handle)
{
    assert(ABoxMqttIdem_TakeExecutable(&store.cache, handle, &clock_now) == 4003);
    poll(2);
    assert(ABoxMqttIdem_TakeExecutable(&store.cache, handle, &clock_now) == 200);
    effects++;
    assert(ABoxMqttIdem_TakeExecutable(&store.cache, handle, &clock_now) == 4003);
}

static void finish(size_t handle, const char *data, size_t size)
{
    assert(ABoxMqttIdem_Complete(&store.cache, handle, data, size));
    assert(!ABoxMqttIdem_Response(&store.cache, handle, NULL));
    poll(8);
    assert(ABoxMqttIdem_State(&store.cache, handle) == ABOX_IDEM_FINAL);
}

static void replay(const char *id, const char *expected, size_t size)
{
    size_t handle, length;
    assert(ABoxMqttIdem_Find(&store.cache, id, digest, &handle) == ABOX_IDEM_REPLAY);
    const char *data = ABoxMqttIdem_Response(&store.cache, handle, &length);
    assert(data && length == size && !memcmp(data, expected, size));
    assert(ABoxMqttIdem_Find(&store.cache, id, conflict, &handle) == ABOX_IDEM_CONFLICT);
}

static void driver_bounds(void)
{
    uint8_t buffer[515], readback[515];
    nor_test_fresh(&chip, &nor);
    memset(buffer, 0x3C, sizeof(buffer));
    assert(ABoxNor_Program(&nor, ABOX_NOR_OTA_SLOT0, 255, buffer, sizeof(buffer)) == ABOX_NOR_OK);
    assert(chip.programs == 4);
    assert(ABoxNor_Read(&nor, ABOX_NOR_OTA_SLOT0, 255, readback, sizeof(readback)) == ABOX_NOR_OK);
    assert(!memcmp(buffer, readback, sizeof(buffer)));
    assert(ABoxNor_Program(&nor, ABOX_NOR_OTA_SLOT0, ABOX_NOR_OTA_SLOT_SIZE - 1,
                          buffer, sizeof(buffer)) == ABOX_NOR_ERROR);
    assert(ABoxNor_Read(&nor, ABOX_NOR_IDEMPOTENCY, ABOX_NOR_IDEM_SIZE - 1U, readback, 1U) == ABOX_NOR_OK);
    assert(ABoxNor_Read(&nor, ABOX_NOR_IDEMPOTENCY, ABOX_NOR_IDEM_SIZE, readback, 1U) == ABOX_NOR_ERROR);
    assert(ABoxNor_Program(&nor, ABOX_NOR_IDEMPOTENCY, ABOX_NOR_IDEM_SIZE - 1U, buffer, 2U) == ABOX_NOR_ERROR);
    assert(ABoxNor_StartErase(&nor, ABOX_NOR_IDEMPOTENCY, ABOX_NOR_IDEM_SIZE) == ABOX_NOR_ERROR);
    assert(ABoxNor_Read(&nor, ABOX_NOR_IDEMPOTENCY, UINT32_MAX, readback, 1) == ABOX_NOR_ERROR);
    assert(ABoxNor_StartErase(&nor, ABOX_NOR_OTA_SLOT0, 1) == ABOX_NOR_ERROR);
    assert(ABoxNor_StartErase(&nor, ABOX_NOR_OTA_SLOT0, 0) == ABOX_NOR_OK);
    assert(ABoxNor_Read(&nor, ABOX_NOR_OTA_SLOT0, 0, readback, 1) == ABOX_NOR_BUSY);
    chip.stuck = 1;
    chip.tick += 500;
    assert(ABoxNor_Status(&nor) == ABOX_NOR_ERROR);
    nor_test_cut_erase(&chip);
    chip.stuck = 0;
    assert(ABoxNor_Read(&nor, ABOX_NOR_OTA_SLOT0, 0, readback, 1) == ABOX_NOR_OK);
    assert(ABoxNor_Program(&nor, ABOX_NOR_OTA_SLOT1, 0, buffer, 1) == ABOX_NOR_ERROR);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static void layout_cuts_and_mismatch(void)
{
    /* A cut before either the header body or the commit word is durable
     * must be recoverable on the next mount of the same configuration. */
    for (unsigned cut = 1; cut <= 2; ++cut) {
        nor_test_fresh(&chip, &nor);
        chip.cut_program = cut;
        assert(ABoxMqttIdemNor_Init(&store, &nor, "device-1"));
        poll(1);
        assert(store.cache.failed && !store.cache.ready);
        mount("device-1");
    }

    size_t handle;
    fresh();
    assert(begin("live", 5000, &handle) == 200);
    dispatch(handle);
    finish(handle, "ok", 2U);

    /* Emulate a different firmware partition size with a valid stored
     * header. Keep log contents in place to detect erase or fail-open. */
    uint8_t *header = chip.bytes + ABOX_NOR_IDEM_OFFSET;
    memcpy(snapshot, chip.bytes, sizeof(snapshot));
    const uint32_t different_size = ABOX_NOR_IDEM_KIB == 3584U ?
        ABOX_NOR_IDEM_SIZE - ABOX_NOR_SECTOR_SIZE : ABOX_NOR_IDEM_SIZE + ABOX_NOR_SECTOR_SIZE;
    put32(header + 4U, different_size);
    put32(header + 8U, ~different_size);
    put32(header + 12U, ABoxNor_Crc32(0, header + 4U, 8U));
    uint32_t programs = chip.programs, bytes_read = chip.bytes_read;
    nor_test_open(&chip, &nor);
    assert(ABoxMqttIdemNor_Init(&store, &nor, "device-1"));
    poll(1000);
    assert(!store.cache.ready && store.cache.failed && !chip.erases && chip.programs == programs);
    assert(chip.bytes_read - bytes_read == 16U);
    assert(begin("live", 5000, &handle) == 4003);
    assert(!memcmp(chip.bytes + ABOX_NOR_IDEM_OFFSET + ABOX_NOR_SECTOR_SIZE,
                   snapshot + ABOX_NOR_IDEM_OFFSET + ABOX_NOR_SECTOR_SIZE,
                   ABOX_NOR_IDEM_SIZE - ABOX_NOR_SECTOR_SIZE));
    /* The mismatch is local to the log and leaves the OTA driver usable. */
    uint8_t byte;
    assert(ABoxNor_Read(&nor, ABOX_NOR_OTA_SLOT1, 0U, &byte, 1U) == ABOX_NOR_OK);
    memcpy(header, snapshot + ABOX_NOR_IDEM_OFFSET, 16U);
    mount("device-1");
    replay("live", "ok", 2U);
}

static void record_cuts(void)
{
    size_t handle;
    fresh();
    assert(begin("old", 5000, &handle) == 200);
    dispatch(handle);
    finish(handle, full_response, sizeof(full_response));
    memcpy(snapshot, chip.bytes, sizeof(snapshot));

    for (unsigned step = 1; step <= 3; ++step) {
        assert(begin("cut", 5000, &handle) == 200);
        chip.cut_program = chip.programs + step;
        poll(2);
        assert(store.cache.failed && ABoxMqttIdem_TakeExecutable(&store.cache, handle, &clock_now) == 4003);
        replay("old", full_response, sizeof(full_response));
        mount("device-1");
        assert(ABoxMqttIdem_Find(&store.cache, "cut", digest, NULL) == ABOX_IDEM_NEW);
        memcpy(chip.bytes, snapshot, sizeof(snapshot));
        mount("device-1");
    }

    assert(begin("prepared", 5000, &handle) == 200);
    dispatch(handle);
    mount("device-1");
    assert(ABoxMqttIdem_Find(&store.cache, "prepared", digest, &handle) == ABOX_IDEM_UNCERTAIN);
    assert(ABoxMqttIdem_TakeExecutable(&store.cache, handle, &clock_now) == 4003);

    memcpy(chip.bytes, snapshot, sizeof(snapshot));
    mount("device-1");
    assert(begin("final-cut", 5000, &handle) == 200);
    dispatch(handle);
    assert(ABoxMqttIdem_Complete(&store.cache, handle, full_response, sizeof(full_response)));
    chip.cut_program = chip.programs + 2;
    poll(8);
    assert(store.cache.failed && effects == 3);
    mount("device-1");
    assert(ABoxMqttIdem_Find(&store.cache, "final-cut", digest, NULL) == ABOX_IDEM_UNCERTAIN);
    replay("old", full_response, sizeof(full_response));
    mount("device-2");
    assert(ABoxMqttIdem_Find(&store.cache, "old", digest, NULL) == ABOX_IDEM_NEW);
    if (ABOX_IDEM_NOR_LOG_SECTORS > 1U) {
        assert(begin("old", 5000, &handle) == 200);
        dispatch(handle);
        finish(handle, "other", 5);
    } else {
        assert(begin("old", 5000, &handle) == 4003);
    }
    mount("device-1");
    replay("old", full_response, sizeof(full_response));
}

static void corrupt_response(void)
{
    size_t handle;
    fresh();
    assert(begin("corrupt", 5000U, &handle) == 200U);
    dispatch(handle);
    finish(handle, full_response, sizeof(full_response));
    assert(begin("good", 5000U, &handle) == 200U);
    dispatch(handle);
    finish(handle, "ok", 2U);

    /* Flip one programmed byte in the known saved response. */
    uint8_t *sector = chip.bytes + ABOX_NOR_IDEM_OFFSET + ABOX_NOR_SECTOR_SIZE;
    unsigned offset;
    for (offset = 0U; offset <= ABOX_NOR_SECTOR_SIZE - sizeof(full_response); ++offset)
        if (!memcmp(sector + offset, full_response, sizeof(full_response))) break;
    assert(offset <= ABOX_NOR_SECTOR_SIZE - sizeof(full_response));
    sector[offset] &= 0xFEU;
    assert(ABoxMqttIdem_Find(&store.cache, "corrupt", digest, &handle) == ABOX_IDEM_UNCERTAIN);
    assert(store.cache.failed && !ABoxMqttIdem_Response(&store.cache, handle, NULL));
    assert(begin("new", 5000U, &handle) == 4003U);
    replay("good", "ok", 2U);
}

static unsigned fill(const char *data, size_t length)
{
    fresh();
    unsigned count;
    for (count = 0; count < 30000; ++count) {
        char id[24];
        size_t handle;
        snprintf(id, sizeof(id), "req-%u", count);
        uint16_t result = begin(id, length == sizeof(full_response) && count == 3U ? 15000 : 5000, &handle);
        if (result == 4003) break;
        assert(result == 200);
        dispatch(handle);
        finish(handle, data, length);
    }
    unsigned expected_per_sector = length == sizeof(full_response) ? 3U : 21U;
    assert(count == expected_per_sector * ABOX_IDEM_NOR_LOG_SECTORS && effects == count && !chip.erases);
    size_t handle;
    clock_now.utc_ms += 606000;
    assert(begin("forward", 5000, &handle) == 4003);
    poll(1000);
    assert(!chip.erases);
    clock_now = initial;
    mount("device-1");
    unsigned replay_count = length == sizeof(full_response) || count < 128U ? count : 128U;
    for (unsigned i = 0; i < replay_count; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "req-%u", i);
        replay(id, data, length);
    }
    uint32_t before = chip.bytes_read;
    clock_t started = clock();
    uint32_t peak = 0U;
    for (unsigned i = 0; i < 128U; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "miss-%u", i);
        uint32_t start_bytes = chip.bytes_read;
        assert(ABoxMqttIdem_Find(&store.cache, id, digest, NULL) == ABOX_IDEM_NEW);
        if (chip.bytes_read - start_bytes > peak) peak = chip.bytes_read - start_bytes;
    }
    printf("size=%zu records=%u avg_miss_spi_bytes=%lu peak_miss_spi_bytes=%lu avg_host_miss_us=%.1f erases=%lu store_bytes=%zu\n",
           length, count, (unsigned long)((chip.bytes_read - before) / 128U), (unsigned long)peak,
           (double)(clock() - started) * 1000000.0 / CLOCKS_PER_SEC / 128.0,
           (unsigned long)chip.erases, sizeof(store));
    return count;
}

static void reclaim_cuts(void)
{
    fill(full_response, sizeof(full_response));
    memcpy(snapshot, chip.bytes, sizeof(snapshot));
    size_t handle;
    clock_now.quality = ABOX_CLOCK_UNTRUSTED;
    assert(begin("new", 5000, &handle) == 4003);
    poll(1000);
    assert(!chip.erases);
    clock_now = initial;
    clock_now.utc_ms += 606000;
    clock_now.mono_ms += 606000;

    for (unsigned fault = 0; fault < 4; ++fault) {
        memcpy(chip.bytes, snapshot, sizeof(snapshot));
        chip.cut_program = chip.erases = 0;
        mount("device-1");
        assert(begin("new", 5000, &handle) == 4003);
        if (fault < 2) {
            chip.cut_program = chip.programs + fault + 1;
            poll(1);
            assert(store.cache.failed && !chip.erases);
        } else {
            unsigned want = fault - 1;
            for (unsigned i = 0; i < 30 && chip.erases < want; ++i) poll(1);
            assert(chip.erases == want);
            nor_test_cut_erase(&chip);
        }
        mount("device-1");
        if (ABOX_IDEM_NOR_LOG_SECTORS > 1U)
            replay("req-3", full_response, sizeof(full_response));
        if (fault < 2) {
            replay("req-0", full_response, sizeof(full_response));
            assert(begin("new", 5000, &handle) == 4003);
            poll(30);
        } else {
            assert(ABoxMqttIdem_Find(&store.cache, "req-0", digest, NULL) == ABOX_IDEM_NEW);
        }
        assert(begin("new", 5000, &handle) == 200);
        dispatch(handle);
        finish(handle, "ok", 2);
        replay("new", "ok", 2);
        printf("reclaim_cut=%u erase_commands=%lu\n", fault, (unsigned long)chip.erases);
    }
}

int main(void)
{
    for (unsigned i = 0; i < sizeof(full_response); ++i) full_response[i] = (char)('a' + i % 26);
    driver_bounds();
    layout_cuts_and_mismatch();
    record_cuts();
    corrupt_response();
    reclaim_cuts();
    (void)fill("ok", 2);
    return 0;
}
