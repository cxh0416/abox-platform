#include "ufs_at.h"
#include <stdio.h>
#include <string.h>

static uint32_t open_handle;
static uint8_t file_open, modem_ready;
#if defined(ABOX_GD_BOOT_RAM_SMOKE)
volatile uint32_t g_abox_ufs_smoke_stage;
volatile uint32_t g_abox_ufs_io_stage, g_abox_ufs_bytes;
char g_abox_ufs_line[128];
#define SMOKE_STAGE(value) (g_abox_ufs_smoke_stage = (value))
#define IO_STAGE(value) (g_abox_ufs_io_stage = (value))
#define IO_LINE(value) do { strncpy(g_abox_ufs_line, (value), sizeof(g_abox_ufs_line) - 1U); g_abox_ufs_line[sizeof(g_abox_ufs_line) - 1U] = 0; } while (0)
#define IO_BYTES(value) (g_abox_ufs_bytes = (value))
#else
#define SMOKE_STAGE(value) ((void)0)
#define IO_STAGE(value) ((void)0)
#define IO_LINE(value) ((void)0)
#define IO_BYTES(value) ((void)0)
#endif

static int at_line(char *line, uint32_t capacity, uint32_t timeout)
{
    uint32_t used = 0U, start = ABoxGdBootTick();
    while ((uint32_t)(ABoxGdBootTick() - start) < timeout) {
        uint8_t byte;
        int received = ABoxGdBootUartRead(&byte, 25U);
        if (received < 0) return -1;
        if (!received) continue;
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (used) { line[used] = '\0'; return 1; }
        } else if (used + 1U < capacity) line[used++] = (char)byte;
        else return -1;
    }
    return 0;
}

static int send_command(const char *command)
{ return ABoxGdBootUartWrite(0, (const uint8_t *)command, (uint32_t)strlen(command)); }
static int parse_value(const char *line, const char *prefix, uint32_t *value)
{
    uint32_t result = 0U;
    size_t length = strlen(prefix);
    if (strncmp(line, prefix, length)) return 0;
    line += length;
    while (*line == ' ') ++line;
    if (*line < '0' || *line > '9') return 0;
    do {
        uint32_t digit = (uint32_t)(*line++ - '0');
        if (result > (UINT32_MAX - digit) / 10U) return 0;
        result = result * 10U + digit;
    } while (*line >= '0' && *line <= '9');
    if (*line) return 0;
    *value = result;
    return 1;
}
static int expect_ok(uint32_t timeout)
{
    char line[128]; uint32_t start = ABoxGdBootTick();
    while ((uint32_t)(ABoxGdBootTick() - start) < timeout) {
        int received = at_line(line, sizeof(line), 1000U);
        if (received < 0) return 0;
        if (!received) continue;
        if (!strcmp(line, "OK")) return 1;
        if (strstr(line, "ERROR")) return 0;
    }
    return 0;
}

static int close_leaked_handles(void)
{
    char line[128], name[64], command[48];
    uint32_t handles[8], count = 0U, start = ABoxGdBootTick();
    int finished = 0, overflow = 0;
    if (!send_command("AT+QFOPEN?\r\n")) return 0;
    while ((uint32_t)(ABoxGdBootTick() - start) < 5000U) {
        unsigned long handle;
        int received = at_line(line, sizeof(line), 1000U);
        if (received < 0) return 0;
        if (!received) continue;
        if (sscanf(line, "+QFOPEN: \"%63[^\"]\",%lu", name, &handle) == 2) {
            if (count < sizeof(handles) / sizeof(handles[0])) handles[count++] = (uint32_t)handle;
            else overflow = 1;
        } else if (!strcmp(line, "OK")) { finished = 1; break; }
        else if (strstr(line, "ERROR")) return 0;
    }
    if (!finished) return 0;
    for (uint32_t i = 0U; i < count; ++i) {
        (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu\r\n", (unsigned long)handles[i]);
        if (!send_command(command) || !expect_ok(5000U)) return 0;
    }
    return !overflow;
}

int ABoxGdBootUfs_Open(void *ctx, uint8_t slot)
{
    char command[64], line[128]; uint32_t handle = 0; int seen = 0;
    uint32_t start = ABoxGdBootTick();
    (void)ctx;
    if (!modem_ready || slot > 1U || file_open) return 0;
    IO_STAGE(10U);
    (void)snprintf(command, sizeof(command), "AT+QFOPEN=\"fw_slot%u.bin\",0\r\n", (unsigned)slot);
    if (!send_command(command)) return 0;
    IO_STAGE(11U);
    while ((uint32_t)(ABoxGdBootTick() - start) < 5000U) {
        int received = at_line(line, sizeof(line), 1000U);
        if (received < 0) return 0;
        if (!received) continue;
        IO_LINE(line);
        if (parse_value(line, "+QFOPEN:", &handle)) { seen = 1; continue; }
        if (!strcmp(line, "OK") && seen) { open_handle = (uint32_t)handle; file_open = 1U; IO_STAGE(15U); return 1; }
        if (strstr(line, "ERROR")) { IO_STAGE(12U); return 0; }
    }
    return 0;
}
int ABoxGdBootUfs_Close(void *ctx)
{
    char command[48]; int ok;
    (void)ctx;
    if (!file_open) return 0;
    (void)snprintf(command, sizeof(command), "AT+QFCLOSE=%lu\r\n", (unsigned long)open_handle);
    ok = send_command(command) && expect_ok(5000U);
    file_open = 0U;
    return ok;
}
int ABoxGdBootUfs_Read(void *ctx, uint8_t *data, uint32_t size)
{
    char command[48], line[128]; uint32_t actual, start = ABoxGdBootTick();
    (void)ctx;
    if (!file_open || !data || !size || size > 1024U) return 0;
    IO_STAGE(20U); IO_BYTES(0U);
    (void)snprintf(command, sizeof(command), "AT+QFREAD=%lu,%lu\r\n", (unsigned long)open_handle, (unsigned long)size);
    if (!send_command(command)) return 0;
    while ((uint32_t)(ABoxGdBootTick() - start) < 5000U) {
        int received = at_line(line, sizeof(line), 1000U);
        if (received < 0) return 0;
        if (!received) continue;
        IO_LINE(line);
        if (parse_value(line, "CONNECT ", &actual)) {
            IO_STAGE(21U);
            if (actual != size) return 0;
            for (uint32_t i = 0; i < size; ++i) {
                if (ABoxGdBootUartRead(&data[i], 1000U) != 1) { IO_STAGE(22U); return 0; }
                IO_BYTES(i + 1U);
            }
            IO_STAGE(23U);
            if (!expect_ok(5000U)) return 0;
            IO_STAGE(25U); return 1;
        }
        if (strstr(line, "ERROR")) return 0;
    }
    return 0;
}

int ABoxGdBootUfs_Init(void)
{
    file_open = 0U; modem_ready = 0U;
    SMOKE_STAGE(1U);
    if (!send_command("ATE0\r\n")) return 0;
    SMOKE_STAGE(2U);
    if (!expect_ok(10000U)) return 0;
    SMOKE_STAGE(3U);
    modem_ready = (uint8_t)close_leaked_handles();
    if (modem_ready) SMOKE_STAGE(4U);
    return modem_ready;
}

#if defined(ABOX_GD_BOOT_UFS_INFO_PROBE)
int ABoxGdBootUfs_GetSize(uint8_t slot, uint32_t *size)
{
    char command[64], line[128];
    uint32_t start = ABoxGdBootTick();
    int found = 0;
    if (!modem_ready || slot > 1U || !size) return 0;
    (void)snprintf(command, sizeof(command), "AT+QFLST=\"fw_slot%u.bin\"\r\n", (unsigned)slot);
    if (!send_command(command)) return 0;
    while ((uint32_t)(ABoxGdBootTick() - start) < 5000U) {
        int received = at_line(line, sizeof(line), 1000U);
        if (received < 0) return 0;
        if (!received) continue;
        if (!strncmp(line, "+QFLST: ", 8U)) {
            const char *comma = strrchr(line, ',');
            uint32_t length;
            if (!comma || !parse_value(comma + 1, "", &length)) return 0;
            *size = length; found = 1; continue;
        }
        if (!strcmp(line, "OK")) return found;
        if (strstr(line, "ERROR")) return 0;
    }
    return 0;
}
#endif
