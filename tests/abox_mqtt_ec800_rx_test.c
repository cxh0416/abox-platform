#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "abox_mqtt_ec800_rx.h"

typedef struct {
    unsigned count;
    uint8_t topic[32], payload[64];
    size_t topic_length, payload_length;
    ABoxMqttEc800Rx *reentry_rx;
} Fixture;

static void message(void *user, const uint8_t *topic, size_t topic_length,
                    const uint8_t *payload, size_t payload_length)
{
    Fixture *f = user;
    assert(topic_length <= sizeof(f->topic));
    assert(payload_length <= sizeof(f->payload));
    memcpy(f->topic, topic, topic_length);
    memcpy(f->payload, payload, payload_length);
    f->topic_length = topic_length;
    f->payload_length = payload_length;
    ++f->count;
    if (f->reentry_rx) {
        static const uint8_t nested[] = "+QMTRECV: 0,0,\"nested\",1,x";
        ABoxMqttEc800Rx_Feed(f->reentry_rx, nested, sizeof(nested) - 1U, 100U);
        ABoxMqttEc800Rx_Reset(f->reentry_rx);
    }
}

static void feed(ABoxMqttEc800Rx *rx, const char *text, uint32_t now)
{ ABoxMqttEc800Rx_Feed(rx, (const uint8_t *)text, strlen(text), now); }

int main(void)
{
    uint8_t header[96], topic[32], payload[64];
    Fixture f = {0};
    ABoxMqttEc800Rx rx;
    assert(!ABoxMqttEc800Rx_Init(&rx, header, sizeof(header), topic,
                                 sizeof(topic), payload, sizeof(payload),
                                 (uint32_t)INT32_MAX + 1U, 1U, message, &f));
    assert(ABoxMqttEc800Rx_Init(&rx, header, sizeof(header), topic,
                                sizeof(topic), payload, sizeof(payload),
                                1000U, 1U, message, &f));
    feed(&rx, "+QMTRE", 1U);
    feed(&rx, "CV: 0,0,\"topic/a\",5,he", 2U);
    assert(f.count == 0U);
    feed(&rx, "llo\r\n+QMTRECV: 0,0,\"topic/b\",3,xyz\r\n", 3U);
    assert(f.count == 2U && f.topic_length == 7U);
    assert(memcmp(f.topic, "topic/b", 7U) == 0);
    assert(f.payload_length == 3U && memcmp(f.payload, "xyz", 3U) == 0);

    feed(&rx, "+QMTRECV: 0,0,\"topic/c\",\"{\\\"a\\\":1}\"\r\n", 4U);
    assert(f.count == 3U && f.payload_length == 7U);
    assert(memcmp(f.payload, "{\"a\":1}", 7U) == 0);

    feed(&rx, "+QMTRECV: 0,0,\"topic/c\",7,\"{\\\"a\\\":1}\"\r\n", 4U);
    assert(f.count == 4U && f.payload_length == 7U);
    assert(memcmp(f.payload, "{\"a\":1}", 7U) == 0);

    feed(&rx, "+QMTRECV: 0,0,\"topic/d\",{\"s\":\"}\",\"v\":", 5U);
    feed(&rx, "1}\r\n", 6U);
    assert(f.count == 5U);
    assert(memcmp(f.payload, "{\"s\":\"}\",\"v\":1}", f.payload_length) == 0);

    {
        uint8_t small_payload[4];
        Fixture small = {0};
        assert(ABoxMqttEc800Rx_Init(&rx, header, sizeof(header), topic,
                                    sizeof(topic), small_payload, sizeof(small_payload),
                                    1000U, 0U, message, &small));
        feed(&rx, "+QMTRECV: 0,0,\"topic\",6,abcdef\r\n", 7U);
        assert(small.count == 0U && rx.counters.overflow == 1U);
        feed(&rx, "+QMTRECV: 0,0,\"topic\",4,ABCD\r\n", 8U);
        assert(small.count == 1U && small.payload_length == 4U);
        feed(&rx, "+QMTRECV: 0,0,\"topic\",4,xy", 9U);
        ABoxMqttEc800Rx_Poll(&rx, 1010U);
        assert(rx.counters.expired == 1U && small.count == 1U);
        assert(ABoxMqttEc800Rx_IsLocked(&rx));
        feed(&rx, "+QMTRECV: 0,0,\"topic\",2,ok\r\n", 1011U);
        assert(small.count == 1U);
        /* The UART owner has drained the stale fragment before resuming. */
        ABoxMqttEc800Rx_Reset(&rx);
        feed(&rx, "+QMTRECV: 0,0,\"topic\",2,ok\r\n", 1011U);
        assert(small.count == 2U);
        small.reentry_rx = &rx;
        feed(&rx, "+QMTRECV: 0,0,\"topic\",2,ok\r\n", 1012U);
        assert(small.count == 3U && rx.counters.completed == 3U);
        feed(&rx, "+QMTRECV: 0,0,\"topic\",\"legacy\"\r\n", 1013U);
        assert(small.count == 3U && rx.counters.rejected >= 2U);
    }
    memset(&f, 0, sizeof(f));
    assert(ABoxMqttEc800Rx_Init(&rx, header, sizeof(header), topic,
                                sizeof(topic), payload, sizeof(payload),
                                1000U, 1U, message, &f));
    feed(&rx, "+QMTRECV: 0,0,\"topic/raw\",\"{\"a\":1}\"\r\n", 2000U);
    assert(f.count == 1U && f.payload_length == 7U);
    assert(memcmp(f.payload, "{\"a\":1}", 7U) == 0);
    feed(&rx, "+QMTRECV: 0,0,\"topic/raw\",7,\"{\"a\":1}\"\r\n", 2001U);
    assert(f.count == 2U && f.payload_length == 7U);
    assert(memcmp(f.payload, "{\"a\":1}", 7U) == 0);

    /* The owner may have sampled its poll time before draining the UART. */
    feed(&rx, "+QMTRECV: 0,0,\"topic/split\",5,he", 2101U);
    ABoxMqttEc800Rx_Poll(&rx, 2100U);
    assert(ABoxMqttEc800Rx_IsCollecting(&rx));
    assert(!ABoxMqttEc800Rx_IsLocked(&rx) && rx.counters.expired == 0U);
    feed(&rx, "llo\r\n", 2102U);
    assert(f.count == 3U && f.payload_length == 5U && memcmp(f.payload, "hello", 5U) == 0);

    feed(&rx, "+QMTRECV: 0,0,\"topic/wrap\",5,he", 0U);
    ABoxMqttEc800Rx_Poll(&rx, UINT32_MAX);
    assert(!ABoxMqttEc800Rx_IsLocked(&rx) && rx.counters.expired == 0U);
    feed(&rx, "llo\r\n", 1U);
    assert(f.count == 4U && f.payload_length == 5U && memcmp(f.payload, "hello", 5U) == 0);

    feed(&rx, "+QMTRECV: 0,0,\"topic/wrap\",5,he", UINT32_MAX - 499U);
    ABoxMqttEc800Rx_Poll(&rx, 499U); /* 999 ms after the last byte. */
    assert(!ABoxMqttEc800Rx_IsLocked(&rx) && rx.counters.expired == 0U);
    ABoxMqttEc800Rx_Poll(&rx, 500U);
    assert(ABoxMqttEc800Rx_IsLocked(&rx) && rx.counters.expired == 1U);
    return 0;
}
