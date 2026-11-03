#include "abox_board_io_stm32f105.h"
#include "stm32f1xx_hal.h"

#include <assert.h>

GPIO_TypeDef gpio_a, gpio_b, gpio_c;
static uint32_t output_config[3], input_config[3], write_mask[3];
static unsigned clocks[3];
static unsigned reads;

static unsigned index_of(GPIO_TypeDef *gpio)
{
    if (gpio == GPIOA) return 0;
    if (gpio == GPIOB) return 1;
    assert(gpio == GPIOC);
    return 2;
}
void mock_clock(unsigned port) { ++clocks[port]; }
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState state)
{
    unsigned i = index_of(gpio);
    write_mask[i] |= pin;
    if (state == GPIO_PIN_SET) gpio->ODR |= pin;
    else gpio->ODR &= (uint32_t)~pin;
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin)
{
    ++reads;
    return (gpio->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
void HAL_GPIO_Init(GPIO_TypeDef *gpio, GPIO_InitTypeDef *config)
{
    unsigned i = index_of(gpio);
    if (config->Mode == GPIO_MODE_OUTPUT_PP) {
        assert(!(gpio->ODR & config->Pin)); /* low before output mode */
        output_config[i] |= config->Pin;
    } else input_config[i] |= config->Pin;
}
int main(void)
{
    const ABoxBoardIoPort *port = ABoxBoardIoStm32_Port();
    ABoxBoardIo io;
    uint8_t level;
    unsigned i;
    gpio_a.ODR = 7U;
    gpio_b.ODR = 7U;
    gpio_c.ODR = 16U;
    /* Input-only preparation must not disturb already enabled outputs/LEDs. */
    ABoxBoardIoStm32_InputsInit();
    assert(gpio_a.ODR == 7U && gpio_b.ODR == 7U && gpio_c.ODR == 16U);
    assert(write_mask[0] == 0 && write_mask[1] == 0 && write_mask[2] == 0);
    assert(output_config[0] == 0 && output_config[1] == 0 && output_config[2] == 0);
    assert(input_config[0] == 0xf0U);
#if ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V1
    assert(input_config[2] == 0 && clocks[2] == 0);
#else
    assert(input_config[2] == 0x0fU);
#endif
    for (i = 0; i < 3; ++i) clocks[i] = input_config[i] = 0;
    /* LED-only initialization does not even clock/configure A or C. */
    assert(ABoxBoardIo_LedsInit(&io, port, 3U) == ABOX_BOARD_IO_OK);
    assert(gpio_a.ODR == 7U && gpio_c.ODR == 16U && gpio_b.ODR == 4U);
    assert(clocks[0] == 0 && clocks[2] == 0);
    assert(output_config[0] == 0 && output_config[2] == 0 && output_config[1] == 3U);
    assert(input_config[0] == 0 && input_config[1] == 0 && input_config[2] == 0);
    ABoxBoardIo_Poll(&io, 1000U);
    assert(gpio_a.ODR == 7U && gpio_c.ODR == 16U && gpio_b.ODR == 4U);
    assert(ABoxBoardIo_Init(&io, port, 0x0fU) == ABOX_BOARD_IO_OK);
    assert(output_config[0] == 0x07U && output_config[1] == 0x07U && output_config[2] == 0x10U);
    assert(input_config[0] == 0xf0U);
#if ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V1
    assert(input_config[2] == 0U);
#else
    assert(input_config[2] == 0x0fU);
#endif
    assert(write_mask[0] == 0x07U && write_mask[1] == 0x07U && write_mask[2] == 0x10U);
    for (i = 0; i < 3; ++i) assert(clocks[i] != 0);
    for (i = 0; i < 4; ++i) {
        assert(ABoxBoardIo_OutputSet(&io, (ABoxBoardIoOutput)i, 1) == ABOX_BOARD_IO_OK);
        assert(ABoxBoardIo_OutputGet(&io, (ABoxBoardIoOutput)i, &level) == ABOX_BOARD_IO_OK && level);
    }
    assert(gpio_a.ODR == 0x07U && gpio_c.ODR == 0x10U);
    for (i = 0; i < 8; ++i) {
#if ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V1
        static const uint8_t expected_pin[4] = {4, 5, 6, 7};
        if (i >= 4U) {
            unsigned previous = reads;
            assert(!port->read_input(0, (ABoxBoardIoInput)i, &level));
            assert(reads == previous);
            continue;
        }
        ABoxBoardTerminal terminal = (ABoxBoardTerminal)(8U + i);
        gpio_c.IDR = 0U;
        gpio_a.IDR = 1U << expected_pin[i];
#else
        static const uint8_t expected_pin[8] = {3, 2, 1, 0, 7, 6, 5, 4};
        ABoxBoardTerminal terminal = (ABoxBoardTerminal)(4U + i);
        gpio_c.IDR = i < 4 ? 1U << expected_pin[i] : 0U;
        gpio_a.IDR = i >= 4 ? 1U << expected_pin[i] : 0U;
#endif
        assert(port->read_input(0, (ABoxBoardIoInput)i, &level) && level == 1U);
        assert(ABoxBoardIoStm32_ReadTerminal(terminal, &level) && level == 1U);
        gpio_c.IDR = gpio_a.IDR = 0U;
        assert(ABoxBoardIoStm32_ReadTerminal(terminal, &level) && level == 0U);
    }
    for (i = 0; i < 4U; ++i) {
        static const int invalid[] = {0, 3, 12, -1};
        unsigned previous = reads;
        level = 0x55U;
        assert(!ABoxBoardIoStm32_ReadTerminal((ABoxBoardTerminal)invalid[i], &level));
        assert(reads == previous && level == 0x55U);
    }
#if ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V1
    for (i = 4U; i < 8U; ++i)
        assert(!ABoxBoardIoStm32_ReadTerminal((ABoxBoardTerminal)i, &level));
#endif
    assert(!ABoxBoardIoStm32_ReadTerminal(ABOX_BOARD_TERMINAL_B8, 0));
    gpio_c.IDR = 0x0fU;
    gpio_a.IDR = 0xf0U;
    ABoxBoardIo_Poll(&io, 1U);
    ABoxBoardIo_Poll(&io, 21U);
    assert(io.inputs.stable_mask == ABOX_BOARD_INPUTS_PRESENT &&
           io.inputs.valid_mask == ABOX_BOARD_INPUTS_PRESENT);
    return 0;
}
