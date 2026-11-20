#undef NDEBUG
#include <assert.h>
#include "abox_board_io.h"
#include "abox_board_v3.h"
#include "gd32f10x.h"

static uint32_t output[3], input[3], output_pins[3], input_pins[3], primask;
uint32_t test_afio_pcf0;

void rcu_periph_clock_enable(uint32_t port) { assert(port <= RCU_AF); }
void gpio_bit_set(uint32_t port, uint32_t pins) { output[port] |= pins; }
void gpio_bit_reset(uint32_t port, uint32_t pins) { output[port] &= ~pins; }
void gpio_bit_write(uint32_t port, uint32_t pin, FlagStatus level)
{
    if (level == SET) gpio_bit_set(port, pin);
    else gpio_bit_reset(port, pin);
}
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pins)
{
    assert(speed == GPIO_OSPEED_2MHZ);
    if (mode == GPIO_MODE_OUT_PP) {
        if (port == GPIOB && pins == 0xC000U) assert((output[port] & pins) == pins);
        else assert(!(output[port] & pins));
        output_pins[port] |= pins;
    } else {
        assert(mode == GPIO_MODE_IN_FLOATING);
        input_pins[port] |= pins;
    }
}
FlagStatus gpio_input_bit_get(uint32_t port, uint32_t pin) { return (input[port] & pin) ? SET : RESET; }
FlagStatus gpio_output_bit_get(uint32_t port, uint32_t pin) { return (output[port] & pin) ? SET : RESET; }
uint32_t test_get_primask(void) { return primask; }
void test_disable_irq(void) { primask = 1U; }
void test_set_primask(uint32_t key) { primask = key; }

int main(void)
{
    ABoxBoardIo io;
    ABoxBoardIoInputs snapshot;
    const ABoxBoardIoPort *port = ABoxBoardIo_HardwarePort();
    output[GPIOA] = 0x800FU;
    output[GPIOB] = 7U;
    output[GPIOC] = 0x50U;
    test_afio_pcf0 = 2U;
    assert(ABoxBoardIo_Init(&io, port, ABOX_BOARD_IO_LEGACY_OUTPUTS) == ABOX_BOARD_IO_IO_FAILED);
    assert(ABoxBoardIo_Init(&io, port, ABOX_BOARD_IO_V3_OUTPUTS) == ABOX_BOARD_IO_OK);
    assert(test_afio_pcf0 == (2UL << 24 | 2U));
    assert(output_pins[GPIOA] == 0x800FU && !output[GPIOA]);
    assert(output_pins[GPIOC] == 0x70U && !output[GPIOC]);
    assert(output_pins[GPIOB] == 0xC007U && output[GPIOB] == 0xC000U);
    assert(!input_pins[GPIOA] && input_pins[GPIOC] == 0x0FU && input_pins[GPIOB] == 0xF0U);
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT1, 1U) == ABOX_BOARD_IO_INVALID_CHANNEL);
    assert(!output[GPIOA]);
    for (unsigned i = 0; i < 4U; ++i) {
        ABoxBoardIoOutput channel = (ABoxBoardIoOutput)(ABOX_BOARD_IO_PWR_OUT1 + i);
        assert(ABoxBoardIo_OutputSet(&io, channel, 1U) == ABOX_BOARD_IO_OK);
        assert(output[GPIOA] == (1U << i));
        assert(ABoxBoardIo_OutputSet(&io, channel, 0U) == ABOX_BOARD_IO_OK);
    }
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_RELAY, 1U) == ABOX_BOARD_IO_OK);
    assert(output[GPIOC] == GPIO_PIN_4);
    ABoxBoardV3_SetBuzzer(1U);
    assert(output[GPIOA] == GPIO_PIN_15);
    ABoxBoardV3_SetBuzzer(0U);
    assert(!output[GPIOA]);

    for (unsigned i = 0; i < 8U; ++i) {
        input[GPIOC] = i < 4U ? 1U << (3U - i) : 0U;
        input[GPIOB] = i >= 4U ? 1U << (11U - i) : 0U;
        uint8_t level;
        for (unsigned j = 0; j < 8U; ++j) {
            assert(ABoxBoardIo_ReadTerminal((ABoxBoardTerminal)(ABOX_BOARD_TERMINAL_B4 + j), &level));
            assert(level == (i == j));
        }
        ABoxBoardIo_Poll(&io, 40U * i);
        ABoxBoardIo_Poll(&io, 40U * i + 20U);
        assert(ABoxBoardIo_InputsGet(&io, &snapshot) == ABOX_BOARD_IO_OK);
        assert(snapshot.stable_mask == (1U << i) && snapshot.valid_mask == 0xFFU);
    }
    return 0;
}
