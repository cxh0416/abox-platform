#include "abox_rtt_console.h"
#include "SEGGER_RTT.h"
#include <stdio.h>
#include <string.h>

/* Dropped bytes are reported separately, after the queue drains. Never block
 * AT callbacks waiting for a debugger. */
static void put(ABoxRttConsole *c, const void *data, uint16_t length)
{
    const uint8_t *p = data;
    uint16_t i;
    if (length > ABOX_RTT_CONSOLE_OUTPUT_SIZE - c->output_count) {
        c->dropped += length;
        return;
    }
    for (i = 0; i < length; ++i)
        c->output[(c->output_head + c->output_count + i) % ABOX_RTT_CONSOLE_OUTPUT_SIZE] = p[i];
    c->output_count += length;
}
static void puts_(ABoxRttConsole *c, const char *s)
{ put(c, s, (uint16_t)strlen(s)); }

static void on_line(ABoxEc800Event event, const uint8_t *data, uint16_t length, void *user)
{
    ABoxRttConsole *c = user;
    if (!c->pending || event != ABOX_EC800_EVENT_LINE ||
        !ABoxEc800At_IsActive(c->at, c->owner)) return;
    put(c, data, length);
    puts_(c, "\r\n");
}
static void on_done(ABoxEc800Result result, void *user)
{
    ABoxRttConsole *c = user;
    if (ABoxEc800At_LineOverflowCount(c->at) != c->line_overflow_before ||
        ABoxEc800At_RxOverflowCount(c->at) != c->rx_overflow_before)
        puts_(c, "ERR modem input overflow; response incomplete\r\n");
    c->pending = 0;
    c->done = 1;
    c->result = (uint8_t)result;
}
static const char *query(const char *s)
{
    if (!strcmp(s, "ufs space")) return "AT+QFLDS=\"UFS\"";
    if (!strcmp(s, "ufs list")) return "AT+QFLST=\"*\"";
    if (!strcmp(s, "modem info")) return "AT+QGMR";
    if (!strncmp(s, "at ", 3)) {
        s += 3;
        if (!strcmp(s, "AT") || !strcmp(s, "AT+QGMR") ||
            !strcmp(s, "AT+CSQ") || !strcmp(s, "AT+CEREG?") ||
            !strcmp(s, "AT+QFLDS=\"UFS\"") || !strcmp(s, "AT+QFLST=\"*\"")) return s;
    }
    return 0;
}
static void command(ABoxRttConsole *c)
{
    const char *q;
    c->input[c->input_len] = 0;
    if (c->input_overflow) puts_(c, "ERR input too long\r\n");
    else if (!c->input_len) { /* ignore empty CRLF half */ }
    else if (!strcmp(c->input, "help"))
        puts_(c, "help | info | log on/off | modem info | ufs space/list | at <supported query>\r\n");
    else if (!strcmp(c->input, "info")) {
        puts_(c, c->info ? c->info : "ABox RTT console"); puts_(c, "\r\n");
    } else if (!strcmp(c->input, "log on")) { c->log_enabled = 1; puts_(c, "log on\r\n"); }
    else if (!strcmp(c->input, "log off")) { c->log_enabled = 0; puts_(c, "log off\r\n"); }
    else if (!(q = query(c->input))) puts_(c, "ERR unsupported command\r\n");
    else if (c->pending || c->done) puts_(c, "ERR query busy\r\n");
    else if (ABoxEc800At_IsBusy(c->at)) puts_(c, "ERR modem busy; retry\r\n");
    else {
        c->line_overflow_before = ABoxEc800At_LineOverflowCount(c->at);
        c->rx_overflow_before = ABoxEc800At_RxOverflowCount(c->at);
        c->pending = 1;
        if (!ABoxEc800At_Submit(c->at, q, c->owner, ABOX_EC800_PRIORITY_NORMAL,
                               5000U, on_done, c)) {
            c->pending = 0;
            puts_(c, "ERR submit failed\r\n");
        } else puts_(c, "query accepted\r\n");
    }
    c->input_len = c->input_overflow = 0;
}
int ABoxRttConsole_Init(ABoxRttConsole *c, ABoxEc800At *at,
                        ABoxEc800Owner owner, const char *info)
{
    unsigned i;
    if (!c || !at || owner < ABOX_EC800_OWNER_PRODUCT_BASE) return 0;
    for (i = 0; i < ABOX_EC800_AT_HANDLER_SIZE; ++i)
        if (at->handlers[i].callback && at->handlers[i].owner == owner) return 0;
    memset(c, 0, sizeof(*c));
    c->at = at; c->owner = owner; c->info = info;
    if (!ABoxEc800At_Register(at, owner, on_line, c)) { c->at = 0; return 0; }
    puts_(c, "ABox RTT console ready; type help\r\n");
    return 1;
}
void ABoxRttConsole_Log(ABoxRttConsole *c, const char *line, uint16_t length)
{
    if (c && c->at && c->log_enabled && line && length) put(c, line, length);
}
void ABoxRttConsole_Poll(ABoxRttConsole *c)
{
    char bytes[32];
    unsigned n, i;
    if (!c || !c->at) return;
    n = SEGGER_RTT_Read(0, bytes, sizeof(bytes));
    for (i = 0; i < n; ++i) {
        unsigned char b = (unsigned char)bytes[i];
        if (b == '\r' || b == '\n') command(c);
        else if (b == 8 || b == 127) { if (c->input_len && !c->input_overflow) --c->input_len; }
        else if (b >= 32 && b <= 126 && !c->input_overflow) {
            if (c->input_len + 1U < sizeof(c->input)) c->input[c->input_len++] = (char)b;
            else c->input_overflow = 1;
        }
    }
    if (!c->output_count && c->dropped) {
        uint32_t lost = c->dropped;
        c->dropped = 0;
        n = (unsigned)snprintf(bytes, sizeof(bytes), "ERR dropped %lu bytes\r\n", (unsigned long)lost);
        if (n < sizeof(bytes)) put(c, bytes, (uint16_t)n);
    }
    if (!c->output_count && c->done) {
        static const char *const results[] = {"OK", "ERROR", "TIMEOUT", "SEND_OK", "SEND_FAIL"};
        puts_(c, "query result: ");
        puts_(c, c->result < 5 ? results[c->result] : "UNKNOWN");
        puts_(c, "\r\n"); c->done = 0;
    }
    if (c->output_count) {
        unsigned count = c->output_count;
        if (count > 64U) count = 64U;
        if (count >= BUFFER_SIZE_UP) count = BUFFER_SIZE_UP - 1U;
        if (count > ABOX_RTT_CONSOLE_OUTPUT_SIZE - c->output_head)
            count = ABOX_RTT_CONSOLE_OUTPUT_SIZE - c->output_head;
        n = SEGGER_RTT_WriteSkipNoLock(0, c->output + c->output_head, count) ? count : 0U;
        c->output_head = (uint16_t)((c->output_head + n) % ABOX_RTT_CONSOLE_OUTPUT_SIZE);
        c->output_count -= (uint16_t)n;
    }
}
