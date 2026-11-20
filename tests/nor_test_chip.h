#ifndef NOR_TEST_CHIP_H
#define NOR_TEST_CHIP_H

#include "abox_nor.h"
#include <assert.h>
#include <string.h>

typedef struct {
    uint8_t bytes[ABOX_NOR_CAPACITY];
    uint32_t tick, address, erase_address, bytes_read, programs, erases, cut_program;
    uint8_t op, selected, wel, erase_polls, stuck;
} NorTestChip;

static void nor_test_select(void *context, int selected)
{
    NorTestChip *c = context;
    c->selected = (uint8_t)selected;
    c->op = 0U;
}

static int nor_test_transfer(void *context, const uint8_t *tx, uint8_t *rx, uint32_t n)
{
    NorTestChip *c = context;
    assert(c->selected);
    if (!c->op) {
        assert(tx && (n == 1U || n == 4U));
        c->op = tx[0];
        if (n == 4U) c->address = (uint32_t)tx[1] << 16 | (uint32_t)tx[2] << 8 | tx[3];
        if (c->op == 0x06U) c->wel = 1U;
        if (c->op == 0x20U) {
            assert(c->wel && n == 4U && !(c->address % ABOX_NOR_SECTOR_SIZE));
            assert(!c->erase_polls && c->address < ABOX_NOR_CAPACITY);
            c->erase_address = c->address;
            c->erase_polls = 4U;
            c->wel = 0U;
            c->erases++;
        }
        return 1;
    }
    switch (c->op) {
    case 0x05U:
        assert(n == 1U && rx);
        *rx = c->wel ? 2U : 0U;
        if (c->stuck || c->erase_polls) *rx |= 1U;
        if (!c->stuck && c->erase_polls && !--c->erase_polls)
            memset(c->bytes + c->erase_address, 0xFF, ABOX_NOR_SECTOR_SIZE);
        return 1;
    case 0x9FU:
        assert(rx && n == 3U);
        rx[0] = 0xC8U; rx[1] = 0x40U; rx[2] = 0x16U;
        return 1;
    case 0x03U:
        assert(rx && !c->erase_polls && n <= ABOX_NOR_CAPACITY - c->address);
        c->bytes_read += n;
        memcpy(rx, c->bytes + c->address, n);
        return 1;
    case 0x02U: {
        assert(tx && c->wel && !c->erase_polls);
        assert(n <= ABOX_NOR_PAGE_SIZE - c->address % ABOX_NOR_PAGE_SIZE);
        assert(n <= ABOX_NOR_CAPACITY - c->address);
        c->wel = 0U;
        int torn = ++c->programs == c->cut_program;
        if (torn) n /= 2U;
        for (uint32_t i = 0U; i < n; ++i) c->bytes[c->address + i] &= tx[i];
        return !torn;
    }
    default:
        assert(0);
        return 0;
    }
}

static uint32_t nor_test_tick(void *context)
{
    return ((NorTestChip *)context)->tick++;
}

static void nor_test_open(NorTestChip *c, ABoxNor *nor)
{
    const ABoxNorSpi bus = {c, nor_test_select, nor_test_transfer, nor_test_tick};
    uint32_t id = 0U;
    int ok = ABoxNor_Open(nor, &bus, &id);
    assert(ok && id == 0xC84016U);
}

static inline void nor_test_cut_erase(NorTestChip *c)
{
    assert(c->erase_polls);
    memset(c->bytes + c->erase_address, 0xFF, ABOX_NOR_SECTOR_SIZE / 2U);
    c->erase_polls = c->wel = c->selected = 0U;
}

static void nor_test_fresh(NorTestChip *c, ABoxNor *nor)
{
    memset(c, 0, sizeof(*c));
    memset(c->bytes, 0xFF, sizeof(c->bytes));
    nor_test_open(c, nor);
}
#endif
