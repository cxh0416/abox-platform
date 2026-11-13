#include "abox_boot_v2_installer.h"
#include <assert.h>
#include <string.h>

enum { BASE = 0x1000, END = 0x1040, PAGE = 16 };
static uint8_t source[64], flash[64], buffer[12];
static unsigned position, opens, closes, begins, ends, erases, writes, reads;
static int fail_open, fail_close, fail_begin, fail_erase, fail_write, fail_readback;
static int corrupt_second, invalid_vector;

static int open_slot(void *ctx, uint8_t slot)
{ (void)ctx; assert(slot == 1); ++opens; position = 0; return opens != (unsigned)fail_open; }
static int read_slot(void *ctx, uint8_t *data, uint32_t length)
{
    (void)ctx; assert(position + length <= sizeof(source));
    memcpy(data, source + position, length);
    if (corrupt_second && opens == 2 && position == 0) data[8] ^= 1U;
    position += length; return 1;
}
static int close_slot(void *ctx) { (void)ctx; ++closes; return closes != (unsigned)fail_close; }
static int begin_flash(void *ctx) { (void)ctx; ++begins; return !fail_begin; }
static int erase_flash(void *ctx, uint32_t address)
{
    (void)ctx; assert(address >= BASE && address + PAGE <= END && !(address % PAGE));
    ++erases; if (fail_erase) return 0;
    memset(flash + address - BASE, 0xFF, PAGE); return 1;
}
static int write_flash(void *ctx, uint32_t address, const uint8_t word[4])
{
    (void)ctx; assert(address >= BASE && address + 4U <= END && !(address & 3U));
    ++writes; if (fail_write) return 0;
    memcpy(flash + address - BASE, word, 4); return 1;
}
static int read_flash(void *ctx, uint32_t address, uint8_t *data, uint32_t length)
{
    (void)ctx; ++reads; if (fail_readback) return 0;
    memcpy(data, flash + address - BASE, length); return 1;
}
static void end_flash(void *ctx) { (void)ctx; ++ends; }
static int vector_ok(void *ctx, const uint8_t vector[8])
{ (void)ctx; return !invalid_vector && vector[0] == 0x20; }
static ABoxBootV2Installer p = {0, open_slot, read_slot, close_slot, begin_flash,
    erase_flash, write_flash, read_flash, end_flash, vector_ok, BASE, END, PAGE, buffer, sizeof(buffer)};

static void reset(void)
{
    memset(source, 0, sizeof(source)); source[0] = 0x20;
    for (unsigned i = 1; i < sizeof(source); ++i) source[i] = (uint8_t)i;
    memset(flash, 0x55, sizeof(flash));
    position = opens = closes = begins = ends = erases = writes = reads = 0;
    fail_open = fail_close = fail_begin = fail_erase = fail_write = fail_readback = 0;
    corrupt_second = invalid_vector = 0;
}
static ABoxBootV2InstallResult install(unsigned size)
{ return ABoxBootV2_Install(&p, 1, size, ABoxBootV2_Crc32(source, size)); }

int main(void)
{
    reset(); assert(install(35) == ABOX_BOOT_V2_INSTALL_OK);
    assert(opens == 2 && closes == 2 && begins == 1 && ends == 1 && erases == 4 && writes == 9);
    assert(!memcmp(source, flash, 35) && flash[35] == 0xFF && flash[63] == 0xFF);
    reset(); assert(install(7) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !opens);
    reset(); assert(install(65) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !opens);
    reset(); assert(ABoxBootV2_Install(&p, 2, 35, 0) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !opens);
    reset(); invalid_vector = 1; assert(install(35) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !begins && closes == 1);
    reset(); assert(ABoxBootV2_Install(&p, 1, 35, 0) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !begins);
    reset(); fail_open = 2; assert(install(35) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !begins);
    reset(); fail_close = 1; assert(install(35) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !begins);
    reset(); fail_begin = 1; assert(install(35) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED && !erases && closes == 2 && !ends);
    reset(); fail_erase = 1; assert(install(35) == ABOX_BOOT_V2_INSTALL_FAILED && erases == 1 && ends == 1);
    reset(); fail_write = 1; assert(install(35) == ABOX_BOOT_V2_INSTALL_FAILED && ends == 1);
    reset(); fail_readback = 1; assert(install(35) == ABOX_BOOT_V2_INSTALL_FAILED && ends == 1);
    reset(); corrupt_second = 1; assert(install(35) == ABOX_BOOT_V2_INSTALL_FAILED && ends == 1);
    reset(); fail_close = 2; assert(install(35) == ABOX_BOOT_V2_INSTALL_FAILED && ends == 1);
    return 0;
}
