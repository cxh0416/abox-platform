#include "abox_board_v3.h"
#include <stddef.h>
#include "abox_board_io.h"
#include "gd32f10x.h"

typedef struct { uint32_t port, pin; } Pin;
static const Pin outputs[ABOX_BOARD_IO_OUTPUT_COUNT] = {
    [ABOX_BOARD_IO_RELAY] = {GPIOC, GPIO_PIN_4},
    [ABOX_BOARD_IO_PWR_OUT1] = {GPIOA, GPIO_PIN_0},
    [ABOX_BOARD_IO_PWR_OUT2] = {GPIOA, GPIO_PIN_1},
    [ABOX_BOARD_IO_P12V_OUT1] = {GPIOA, GPIO_PIN_2},
    [ABOX_BOARD_IO_P12V_OUT2] = {GPIOA, GPIO_PIN_3},
};

static int valid_output(ABoxBoardIoOutput output)
{
    return (unsigned)output < ABOX_BOARD_IO_OUTPUT_COUNT && outputs[output].pin;
}

static void output_low(uint32_t port, uint32_t pins)
{
    gpio_bit_reset(port, pins);
    gpio_init(port, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, pins);
}

static int prepare_leds(void *context, uint8_t mask)
{
    (void)context;
    if (!mask || (mask & ~7U)) return 0;
    rcu_periph_clock_enable(RCU_GPIOB);
    output_low(GPIOB, mask);
    return 1;
}

static int prepare(void *context)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_AF);
    AFIO_PCF0 = (AFIO_PCF0 & ~AFIO_PCF0_SWJ_CFG) | (2UL << 24);
    output_low(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_15);
    output_low(GPIOC, GPIO_PIN_4 | GPIO_PIN_6);
    /* Preserve SIM selection if a product initializes BoardIo again. */
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_5);
    gpio_bit_set(GPIOB, GPIO_PIN_14 | GPIO_PIN_15);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_14 | GPIO_PIN_15);
    gpio_init(GPIOC, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, 0x000FU);
    gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, 0x00F0U);
    return prepare_leds(context, 7U);
}

static int write_output(void *context, ABoxBoardIoOutput output, uint8_t level)
{
    (void)context;
    if (!valid_output(output)) return 0;
    gpio_bit_write(outputs[output].port, outputs[output].pin, level ? SET : RESET);
    return 1;
}

static int read_output(void *context, ABoxBoardIoOutput output, uint8_t *level)
{
    (void)context;
    if (!level || !valid_output(output)) return 0;
    *level = gpio_output_bit_get(outputs[output].port, outputs[output].pin) != RESET;
    return 1;
}

static int read_output_pin(void *context, ABoxBoardIoOutput output, uint8_t *level)
{
    (void)context;
    if (!level || !valid_output(output)) return 0;
    *level = gpio_input_bit_get(outputs[output].port, outputs[output].pin) != RESET;
    return 1;
}

static int read_input(void *context, ABoxBoardIoInput input, uint8_t *level)
{
    (void)context;
    unsigned index = (unsigned)input;
    if (index >= ABOX_BOARD_IO_INPUT_COUNT || !level) return 0;
    *level = gpio_input_bit_get(index < 4U ? GPIOC : GPIOB,
        1U << (index < 4U ? 3U - index : 11U - index)) != RESET;
    return 1;
}

int ABoxBoardIo_ReadTerminal(ABoxBoardTerminal terminal, uint8_t *level)
{
    return read_input(NULL, (ABoxBoardIoInput)((unsigned)terminal - ABOX_BOARD_TERMINAL_B4), level);
}

static int write_led(void *context, ABoxBoardIoLed led, uint8_t level)
{
    (void)context;
    if ((unsigned)led >= ABOX_BOARD_IO_LED_COUNT) return 0;
    gpio_bit_write(GPIOB, 1U << led, level ? SET : RESET);
    return 1;
}

void ABoxBoardV3_SetBuzzer(uint8_t on)
{
    gpio_bit_write(GPIOA, GPIO_PIN_15, on ? SET : RESET);
}

static uint32_t enter_critical(void *context)
{
    (void)context;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    return key;
}

static void exit_critical(void *context, uint32_t key)
{
    (void)context;
    __set_PRIMASK(key);
}

const ABoxBoardIoPort *ABoxBoardIo_HardwarePort(void)
{
    static const ABoxBoardIoPort port = {
        .prepare = prepare, .write_output = write_output, .read_output = read_output,
        .read_input = read_input, .write_led = write_led, .enter_critical = enter_critical,
        .exit_critical = exit_critical, .prepare_leds = prepare_leds,
        .read_output_pin = read_output_pin, .valid_outputs = ABOX_BOARD_IO_V3_OUTPUTS
    };
    return &port;
}
