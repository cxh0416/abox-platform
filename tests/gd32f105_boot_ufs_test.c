#include "ufs_at.h"
#include <assert.h>
#include <string.h>

static uint8_t rx[1024];
static unsigned rx_read, rx_end, tick_count, commands, fail_at;
static int mode;
static const uint8_t payload[8] = {0, '\r', '\n', 'O', 'K', 0xFF, 'x', 0};
static void append(const void *bytes, unsigned size)
{ assert(rx_end + size <= sizeof(rx)); memcpy(rx + rx_end, bytes, size); rx_end += size; }
static void text(const char *s) { append(s, (unsigned)strlen(s)); }
uint32_t ABoxGdBootTick(void) { return tick_count; }
int ABoxGdBootUartRead(uint8_t *data, uint32_t timeout)
{
    ++tick_count;
    if (fail_at && rx_read == fail_at) { fail_at = 0; return -1; }
    if (rx_read == rx_end) { tick_count += timeout; return 0; }
    *data = rx[rx_read++]; return 1;
}
int ABoxGdBootUartWrite(void *ctx, const uint8_t *data, uint32_t length)
{
    char command[96]; (void)ctx; ++commands; assert(length < sizeof(command));
    memcpy(command, data, length); command[length] = 0;
    if (!strcmp(command, "ATE0\r\n")) text("\r\nOK\r\n");
    else if (!strcmp(command, "AT+QFOPEN?\r\n")) text("\r\n+QFOPEN: \"old\",4\r\n\r\nOK\r\n");
    else if (!strcmp(command, "AT+QFCLOSE=4\r\n") || !strcmp(command, "AT+QFCLOSE=9\r\n")) text("\r\nOK\r\n");
    else if (!strcmp(command, "AT+QFOPEN=\"fw_slot1.bin\",0\r\n")) text("\r\n+QFOPEN: 9\r\n\r\nOK\r\n");
    else if (!strcmp(command, "AT+QFREAD=9,8\r\n")) {
        if (mode == 1) text("\r\nCONNECT 7\r\n");
        else {
            text("\r\nCONNECT 8\r\n");
            if (mode == 2) fail_at = rx_end + 3U;
            append(payload, sizeof(payload)); text("\r\nOK\r\n");
        }
    } else assert(0);
    return 1;
}
static void reset(void) { rx_read = rx_end = tick_count = commands = fail_at = 0; mode = 0; }
int main(void)
{
    uint8_t data[8];
    reset(); assert(ABoxGdBootUfs_Init() && commands == 3);
    assert(!ABoxGdBootUfs_Open(0, 2));
    assert(ABoxGdBootUfs_Open(0, 1));
    assert(!ABoxGdBootUfs_Open(0, 1));
    assert(ABoxGdBootUfs_Read(0, data, sizeof(data)) && !memcmp(data, payload, sizeof(data)));
    assert(ABoxGdBootUfs_Close(0));
    reset(); assert(ABoxGdBootUfs_Init() && ABoxGdBootUfs_Open(0, 1));
    mode = 1; assert(!ABoxGdBootUfs_Read(0, data, sizeof(data)));
    reset(); assert(ABoxGdBootUfs_Init() && ABoxGdBootUfs_Open(0, 1));
    mode = 2; assert(!ABoxGdBootUfs_Read(0, data, sizeof(data)));
    return 0;
}
