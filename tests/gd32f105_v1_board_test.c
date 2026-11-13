#include <assert.h>
#include <stdint.h>
#include "gd32f10x.h"
#include "abox_board_io.h"

static uint32_t output[3], input[3], output_config[3], input_config[3], clocks[3], primask;
void rcu_periph_clock_enable(uint32_t port) { ++clocks[port]; }
void gpio_bit_set(uint32_t port, uint32_t pin) { output[port] |= pin; }
void gpio_bit_reset(uint32_t port, uint32_t pin) { output[port] &= ~pin; }
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pin)
{
    assert(speed == GPIO_OSPEED_2MHZ);
    if (mode == GPIO_MODE_OUT_PP) { assert(!(output[port] & pin)); output_config[port] |= pin; }
    else { assert(mode == GPIO_MODE_IN_FLOATING); input_config[port] |= pin; }
}
FlagStatus gpio_input_bit_get(uint32_t port, uint32_t pin) { return (input[port] & pin) ? SET : RESET; }
FlagStatus gpio_output_bit_get(uint32_t port, uint32_t pin) { return (output[port] & pin) ? SET : RESET; }
uint32_t test_get_primask(void) { return primask; }
void test_disable_irq(void) { primask = 1U; }
void test_set_primask(uint32_t key) { primask = key; }

int main(void)
{
    const ABoxBoardIoPort *port = ABoxBoardIo_HardwarePort();
    ABoxBoardIo io;
    uint8_t level = 9U;
    output[0] = 7U; output[1] = 7U; output[2] = 16U;
    assert(ABoxBoardIo_LedsInit(&io, port, 3U) == ABOX_BOARD_IO_OK);
    assert(output[0] == 7U && output[1] == 4U && output[2] == 16U);
    assert(clocks[0] == 0U && clocks[1] == 1U && clocks[2] == 0U);
    assert(ABoxBoardIo_Init(&io, port, 15U) == ABOX_BOARD_IO_OK);
    assert(output[0] == 0U && output[1] == 0U && output[2] == 0U);
    assert(output_config[0] == 7U && output_config[1] == 7U && output_config[2] == 16U);
    assert(input_config[0] == 0xf0U && input_config[2] == 0U);
    for (unsigned i = 0; i < 4U; ++i) {
        input[0] = 1U << (4U + i);
        assert(port->read_input(0, (ABoxBoardIoInput)i, &level) && level == 1U);
        assert(ABoxBoardIo_ReadTerminal((ABoxBoardTerminal)(ABOX_BOARD_TERMINAL_B8 + i), &level) && level == 1U);
    }
    assert(!port->read_input(0, ABOX_BOARD_IO_IN5, &level));
    level = 0x55U;
    assert(!ABoxBoardIo_ReadTerminal(ABOX_BOARD_TERMINAL_B4, &level) && level == 0x55U);
    assert(!ABoxBoardIo_ReadTerminal((ABoxBoardTerminal)12, &level) && level == 0x55U);
    assert(!ABoxBoardIo_ReadTerminal(ABOX_BOARD_TERMINAL_B8, 0));
    assert(!port->read_input(0, ABOX_BOARD_IO_IN1, 0));
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT1, 1U) == ABOX_BOARD_IO_OK);
    input[0] = 0U;
    assert(port->read_output(0, ABOX_BOARD_IO_OUT1, &level) && level == 1U);
    assert(port->read_output_pin(0, ABOX_BOARD_IO_OUT1, &level) && level == 0U);
    assert(!port->read_output_pin(0, (ABoxBoardIoOutput)9, &level));
    assert(ABoxBoardIo_LedSet(&io, ABOX_BOARD_IO_LED3, ABOX_BOARD_IO_LED_ON, 0U, 0U) == ABOX_BOARD_IO_OK);
    assert(output[1] == 4U);
    primask = 1U;
    assert(ABoxBoardIo_OutputSet(&io, ABOX_BOARD_IO_OUT2, 1U) == ABOX_BOARD_IO_OK && primask == 1U);
    return 0;
}
