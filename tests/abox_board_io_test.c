#include "abox_board_io.h"

#include <assert.h>
#include <stdint.h>

typedef struct {
    uint8_t output[4], input[8], led[3];
    uint8_t fail_input, fail_readback, fail_prepare;
    unsigned writes;
} Fake;

static int prepare(void *context)
{
    Fake *f = context;
    unsigned i;
    for (i = 0; i < 4; ++i) f->output[i] = 0;
    for (i = 0; i < 3; ++i) f->led[i] = 0;
    return !f->fail_prepare;
}
static int write_output(void *context, ABoxBoardIoOutput output, uint8_t on)
{
    Fake *f = context;
    f->output[output] = on;
    ++f->writes;
    return 1;
}
static int read_output(void *context, ABoxBoardIoOutput output, uint8_t *on)
{
    Fake *f = context;
    if (f->fail_readback) return 0;
    *on = f->output[output];
    return 1;
}
static int read_input(void *context, ABoxBoardIoInput input, uint8_t *on)
{
    Fake *f = context;
    if (f->fail_input & (1U << input)) return 0;
    *on = f->input[input];
    return 1;
}
static int write_led(void *context, ABoxBoardIoLed led, uint8_t on)
{
    Fake *f = context;
    f->led[led] = on;
    return 1;
}
static uint32_t enter(void *context) { (void)context; return 0; }
static void leave(void *context, uint32_t key) { (void)context; (void)key; }
static int prepare_leds(void *context, uint8_t mask)
{
    Fake *f = context;
    for (unsigned i = 0; i < 3; ++i) if (mask & (1U << i)) f->led[i] = 0;
    return 1;
}

int main(void)
{
    Fake f = {0};
    ABoxBoardIo io;
    ABoxBoardIoInputs sample;
    ABoxBoardIoPort port = {&f, prepare, write_output, read_output,
                           read_input, write_led, enter, leave, prepare_leds, 0};
    unsigned i;
    f.output[0] = f.output[1] = f.output[2] = f.output[3] = 1;
    f.led[0] = f.led[1] = f.led[2] = 1;
    f.fail_prepare = 1;
    assert(ABoxBoardIo_Init(&io, &port, 1U) == ABOX_BOARD_IO_IO_FAILED);
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT1, 1) == ABOX_BOARD_IO_NOT_READY);
    f.fail_prepare = 0;
    assert(ABoxBoardIo_Init(&io, &port, 1U) == ABOX_BOARD_IO_OK);
    for (i = 0; i < 4; ++i) assert(f.output[i] == 0);
    for (i = 0; i < 3; ++i) assert(f.led[i] == 0);
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT2, 1) == ABOX_BOARD_IO_DENIED);
    assert(f.output[1] == 0);
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_RELAY, 1) == ABOX_BOARD_IO_DENIED);
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT1, 1) == ABOX_BOARD_IO_OK);
    assert(ABoxBoardIo_OutputGet(&io, ABOX_BOARD_IO_OUT1, &f.input[0]) == ABOX_BOARD_IO_OK);
    assert(f.input[0] == 1);
    f.fail_readback = 1;
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT1, 1) == ABOX_BOARD_IO_READBACK_FAILED);
    assert(f.output[0] == 0);
    assert(ABoxBoardIo_OutputGet(&io, ABOX_BOARD_IO_OUT1, &f.input[0]) == ABOX_BOARD_IO_READBACK_FAILED);
    f.fail_readback = 0;
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT1, 0) == ABOX_BOARD_IO_OK);
    assert(ABoxBoardIo_OutputSet(&io, (ABoxBoardIoOutput)9, 1) == ABOX_BOARD_IO_INVALID_CHANNEL);
    for (i = 0; i < 8; ++i) f.input[i] = 1;
    ABoxBoardIo_Poll(&io, UINT32_MAX - 10U);
    assert(ABoxBoardIo_InputsGet(&io, &sample) == ABOX_BOARD_IO_OK);
    assert(sample.raw_mask == 0xffU && sample.valid_mask == 0U);
    ABoxBoardIo_Poll(&io, 10U);
    ABoxBoardIo_InputsGet(&io, &sample);
    assert(sample.stable_mask == 0xffU && sample.valid_mask == 0xffU);
    f.input[3] = 0;
    ABoxBoardIo_Poll(&io, 20U);
    ABoxBoardIo_InputsGet(&io, &sample);
    assert(sample.raw_mask == 0xf7U && sample.stable_mask == 0xffU);
    f.fail_input = 1U << 5;
    ABoxBoardIo_Poll(&io, 40U);
    ABoxBoardIo_InputsGet(&io, &sample);
    assert(sample.stable_mask == 0xf7U && !(sample.valid_mask & (1U << 5)));
    f.fail_input = 0;
    ABoxBoardIo_Poll(&io, 50U);
    ABoxBoardIo_Poll(&io, 70U);
    ABoxBoardIo_InputsGet(&io, &sample);
    assert(sample.valid_mask == 0xffU && sample.sampled_at_ms == 70U);
    assert(ABoxBoardIo_LedSet(&io, ABOX_BOARD_IO_LED2,
                              ABOX_BOARD_IO_LED_BLINK, 100U, 70U) == ABOX_BOARD_IO_OK);
    assert(f.led[1] == 1U);
    ABoxBoardIo_Poll(&io, 120U);
    assert(f.led[1] == 0U);
    assert(ABoxBoardIo_LedSet(&io, ABOX_BOARD_IO_LED2,
                              ABOX_BOARD_IO_LED_ON, 0U, 121U) == ABOX_BOARD_IO_OK);
    ABoxBoardIo_Poll(&io, 1000U);
    assert(f.led[1] == 1U);
    f.output[0] = f.output[1] = f.output[2] = f.output[3] = 1;
    f.led[2] = 1;
    assert(ABoxBoardIo_LedsInit(&io, &port, 3U) == ABOX_BOARD_IO_OK);
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT1, 0) == ABOX_BOARD_IO_NOT_READY);
    assert(ABoxBoardIo_InputsGet(&io, &sample) == ABOX_BOARD_IO_NOT_READY);
    assert(ABoxBoardIo_LedSet(&io, ABOX_BOARD_IO_LED3, ABOX_BOARD_IO_LED_OFF, 0, 0) == ABOX_BOARD_IO_DENIED);
    for (i = 0; i < 4; ++i) assert(f.output[i] == 1);
    assert(f.led[2] == 1);
    assert(ABoxBoardIo_LedPattern(&io, ABOX_BOARD_IO_LED1, 100, 150, 3, 1500, UINT32_MAX - 100) == ABOX_BOARD_IO_OK);
    for (i = 0; i < 4500; ++i) {
        uint32_t now = UINT32_MAX - 100 + i;
        uint32_t phase = i % 2250;
        /* Repeated same business selection must not continually restart pulses. */
        assert(ABoxBoardIo_LedPattern(&io, ABOX_BOARD_IO_LED1, 100, 150, 3, 1500, now) == ABOX_BOARD_IO_OK);
        ABoxBoardIo_Poll(&io, now);
        assert(f.led[0] == (phase < 750 && phase % 250 < 100));
        assert(f.led[2] == 1);
    }
    assert(ABoxBoardIo_LedPattern(&io, ABOX_BOARD_IO_LED1, UINT32_MAX, 1, 2, 1, 0) == ABOX_BOARD_IO_IO_FAILED);
    return 0;
}
