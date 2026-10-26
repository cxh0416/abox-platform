#include "abox_mqtt_backoff.h"
#include <string.h>
static uint32_t random32(ABoxMqttBackoff *b)
{
    uint32_t x = b->random;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    b->random = x; return x;
}
void ABoxMqttBackoff_Init(ABoxMqttBackoff *b, uint32_t seed)
{
    if (b) { memset(b, 0, sizeof(*b)); b->random = seed ? seed : 0x9e3779b9U; }
}
uint32_t ABoxMqttBackoff_Initial(ABoxMqttBackoff *b)
{ return b ? random32(b) % 4001U : 0U; }
uint32_t ABoxMqttBackoff_Failed(ABoxMqttBackoff *b)
{
    static const uint32_t limits[] = {4000U, 8000U, 16000U, 32000U, 60000U};
    uint32_t limit;
    if (!b) return 0;
    limit = limits[b->failures];
    if (b->failures < 4U) ++b->failures;
    b->online = 0;
    return limit / 2U + random32(b) % (limit / 2U + 1U);
}
void ABoxMqttBackoff_Online(ABoxMqttBackoff *b, uint32_t now)
{ if (b && !b->online) { b->online = 1; b->ready_since = now; } }
void ABoxMqttBackoff_Poll(ABoxMqttBackoff *b, uint32_t now)
{ if (b && b->online && (uint32_t)(now - b->ready_since) >= 300000U) b->failures = 0; }
