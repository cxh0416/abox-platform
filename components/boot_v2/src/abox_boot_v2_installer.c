#include "abox_boot_v2_installer.h"
#include <string.h>

static uint32_t update_crc(uint32_t crc, const uint8_t *data, uint32_t length)
{
    while (length--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

static int valid_port(const ABoxBootV2Installer *p, uint8_t slot, uint32_t size)
{
    return p && p->open && p->read && p->close && p->flash_begin && p->flash_erase &&
        p->flash_write && p->flash_read && p->flash_end && p->vector_valid && p->buffer &&
        p->buffer_size >= 8U && !(p->buffer_size & 3U) && p->page_size &&
        !(p->page_size & 3U) && p->app_start < p->app_end &&
        !(p->app_start % p->page_size) && !(p->app_end % p->page_size) &&
        slot < 2U && size >= 8U && size <= p->app_end - p->app_start;
}

static int preflight(const ABoxBootV2Installer *p, uint8_t slot, uint32_t size, uint32_t expected)
{
    uint32_t offset = 0U, crc = 0xFFFFFFFFU;
    int ok = 1;
    if (!p->open(p->context, slot)) return 0;
    while (offset < size) {
        uint32_t n = size - offset;
        if (n > p->buffer_size) n = p->buffer_size;
        if (!p->read(p->context, p->buffer, n) ||
            (!offset && !p->vector_valid(p->context, p->buffer))) { ok = 0; break; }
        crc = update_crc(crc, p->buffer, n);
        offset += n;
    }
    if (!p->close(p->context)) ok = 0;
    return ok && (crc ^ 0xFFFFFFFFU) == expected;
}

ABoxBootV2InstallResult ABoxBootV2_Install(const ABoxBootV2Installer *p,
                                          uint8_t slot, uint32_t size, uint32_t expected)
{
    uint32_t crc = 0xFFFFFFFFU, offset = 0U;
    ABoxBootV2InstallResult result = ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED;
    int flash_open = 0;
    if (!valid_port(p, slot, size) || !preflight(p, slot, size, expected) ||
        !p->open(p->context, slot)) return result;
    if (!p->flash_begin(p->context)) goto done;
    flash_open = 1;
    /* An attempted erase may have modified Flash even if it reports failure. */
    result = ABOX_BOOT_V2_INSTALL_FAILED;
    for (uint32_t address = p->app_start; address < p->app_end; address += p->page_size)
        if (!p->flash_erase(p->context, address)) goto done;
    while (offset < size) {
        uint32_t n = size - offset;
        if (n > p->buffer_size) n = p->buffer_size;
        if (!p->read(p->context, p->buffer, n)) goto done;
        crc = update_crc(crc, p->buffer, n);
        for (uint32_t i = 0; i < n; i += 4U) {
            uint8_t word[4] = {0xFFU, 0xFFU, 0xFFU, 0xFFU}, verify[4];
            uint32_t count = n - i < 4U ? n - i : 4U;
            memcpy(word, p->buffer + i, count);
            if (!p->flash_write(p->context, p->app_start + offset + i, word) ||
                !p->flash_read(p->context, p->app_start + offset + i, verify, 4U) ||
                memcmp(word, verify, 4U)) goto done;
        }
        offset += n;
    }
    if ((crc ^ 0xFFFFFFFFU) == expected) result = ABOX_BOOT_V2_INSTALL_OK;
done:
    if (flash_open) p->flash_end(p->context);
    if (!p->close(p->context)) result = flash_open ? ABOX_BOOT_V2_INSTALL_FAILED : result;
    return result;
}
