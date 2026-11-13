#include "abox_rtt_console.h"
#include "SEGGER_RTT.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr, "failed: %s at %d\n", #x, __LINE__); exit(1); } } while (0)
#if defined(WIN32)
void OS_SIM_EnterCriticalSection(void) {}
void OS_SIM_LeaveCriticalSection(void) {}
#endif
static uint32_t tick;
static char sent[300], output[16384];
static size_t used;
static uint32_t now(void *ctx) { (void)ctx; return tick; }
static int tx(void *ctx, const uint8_t *p, uint16_t n, uint32_t timeout)
{ (void)ctx; (void)timeout; assert(n < sizeof(sent)); memcpy(sent, p, n); sent[n] = 0; return 1; }
static void pump(ABoxRttConsole *c, ABoxEc800At *at, int count)
{
    char buf[256]; unsigned n;
    while (count--) {
        ABoxRttConsole_Poll(c); ABoxEc800At_Task(at);
        while ((n = SEGGER_RTT_ReadUpBuffer(0, buf, sizeof(buf))) != 0) {
            assert(used + n < sizeof(output)); memcpy(output + used, buf, n); used += n; output[used] = 0;
        }
    }
}
static void input(ABoxRttConsole *c, ABoxEc800At *at, const char *s)
{
    while (*s) {
        unsigned count = (unsigned)strlen(s);
        if (count > 8U) count = 8U;
        unsigned n = SEGGER_RTT_WriteDownBuffer(0, s, count);
        s += n; pump(c, at, 1);
    }
    pump(c, at, 10);
}
static void feed(ABoxEc800At *at, const char *s)
{ ABoxEc800At_Feed(at, (const uint8_t *)s, (uint16_t)strlen(s)); }
int main(void)
{
    ABoxEc800At at; ABoxRttConsole c, collision;
    ABoxEc800AtPort port = {0, now, tx, 0, 0};
    SEGGER_RTT_Init(); assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxRttConsole_Init(&c, &at, 30, "test build"));
    assert(!ABoxRttConsole_Init(&collision, &at, 30, "collision"));
    pump(&c, &at, 10); input(&c, &at, "help\r\ninfo\n");
    assert(strstr(output, "ufs space/list") && strstr(output, "test build"));
    input(&c, &at, "at AT+QFDEL=\"*\"\n"); assert(strstr(output, "ERR unsupported"));
    input(&c, &at, "ufs space\n"); assert(strstr(sent, "AT+QFLDS=\"UFS\""));
    input(&c, &at, "ufs list\n"); assert(strstr(output, "ERR query busy"));
    feed(&at, "+QFLDS: 123,456\r\nOK\r\n"); pump(&c, &at, 20);
    assert(strstr(output, "+QFLDS: 123,456") && strstr(output, "query result: OK"));
    input(&c, &at, "ufs list\n");
    for (int i = 0; i < 31; ++i) feed(&at, "+QFLST: \"test.bin\",123\r\n");
    feed(&at, "OK\r\n"); pump(&c, &at, 100);
    assert(!strstr(output, "ERR dropped"));
    { const char *p = output; int count = 0; while ((p = strstr(p, "+QFLST:"))) { ++count; ++p; } assert(count == 31); }
    input(&c, &at, "at AT+CSQ\n"); tick += 6000; ABoxEc800At_Task(&at); pump(&c, &at, 30);
    assert(strstr(output, "query result: TIMEOUT"));
    input(&c, &at, "at AT+CSQ\n");
    ABoxEc800At_SetRxOverflowCount(&at, 1);
    feed(&at, "ERROR\r\n"); pump(&c, &at, 30);
    assert(strstr(output, "ERR modem input overflow; response incomplete"));
    assert(strstr(output, "query result: ERROR"));
    { char long_input[300]; memset(long_input, 'a', sizeof(long_input));
      long_input[sizeof(long_input)-2] = '\n'; long_input[sizeof(long_input)-1] = 0;
      input(&c, &at, long_input); assert(strstr(output, "ERR input too long")); }
    input(&c, &at, "log on\n"); ABoxRttConsole_Log(&c, "hello log\r\n", 11); pump(&c, &at, 10);
    assert(strstr(output, "hello log"));
    /* No host reads: queue and RTT fill, then a visible loss marker follows. */
    _SEGGER_RTT.aUp[0].Flags = SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL;
    for (int i = 0; i < 30; ++i) ABoxRttConsole_Poll(&c);
    for (int i = 0; i < 1000; ++i) ABoxRttConsole_Log(&c, "0123456789\n", 11);
    for (int i = 0; i < 100; ++i) ABoxRttConsole_Poll(&c);
    pump(&c, &at, 100); assert(strstr(output, "ERR dropped"));
    puts("RTT console checks passed"); return 0;
}
