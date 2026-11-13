#include "abox_board_io.h"
#include "abox_v1_gpio.h"
#include "gd32f10x.h"

typedef struct { uint32_t port; uint32_t pin; } BoardPin;
#define PIN(n) (1UL << (n))
static const BoardPin outputs[ABOX_BOARD_IO_OUTPUT_COUNT] = {
    {GPIOA, PIN(ABOX_V1_OUT1_PIN)}, {GPIOA, PIN(ABOX_V1_OUT2_PIN)},
    {GPIOA, PIN(ABOX_V1_OUT3_PIN)}, {GPIOC, PIN(ABOX_V1_RELAY_PIN)}
};
static const BoardPin leds[ABOX_BOARD_IO_LED_COUNT] = {
    {GPIOB, PIN(ABOX_V1_LED1_PIN)}, {GPIOB, PIN(ABOX_V1_LED2_PIN)},
    {GPIOB, PIN(ABOX_V1_LED3_PIN)}
};

static int prepare(void *context)
{
    (void)context;
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_bit_reset(GPIOA, PIN(0) | PIN(1) | PIN(2));
    gpio_bit_reset(GPIOB, PIN(0) | PIN(1) | PIN(2));
    /* Both HVD230 transceivers start in standby before vehicle power is on. */
    gpio_bit_set(GPIOB, PIN(14) | PIN(15));
    gpio_bit_reset(GPIOC, PIN(4));
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, PIN(0) | PIN(1) | PIN(2));
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, PIN(0) | PIN(1) | PIN(2));
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, PIN(14) | PIN(15));
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, PIN(4));
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,
              PIN(4) | PIN(5) | PIN(6) | PIN(7));
    return 1;
}

static int prepare_leds(void *context, uint8_t mask)
{
    (void)context;
    if (!mask || (mask & (uint8_t)~7U)) return 0;
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_bit_reset(GPIOB, mask);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, mask);
    return 1;
}

static int write_output(void *context, ABoxBoardIoOutput output, uint8_t level)
{
    (void)context;
    if ((unsigned)output >= ABOX_BOARD_IO_OUTPUT_COUNT) return 0;
    if (level) gpio_bit_set(outputs[output].port, outputs[output].pin);
    else gpio_bit_reset(outputs[output].port, outputs[output].pin);
    return 1;
}

static int read_output(void *context, ABoxBoardIoOutput output, uint8_t *level)
{
    (void)context;
    if ((unsigned)output >= ABOX_BOARD_IO_OUTPUT_COUNT || !level) return 0;
    *level = gpio_output_bit_get(outputs[output].port, outputs[output].pin) != RESET;
    return 1;
}

static int read_output_pin(void *context, ABoxBoardIoOutput output, uint8_t *level)
{
    (void)context;
    if ((unsigned)output >= ABOX_BOARD_IO_OUTPUT_COUNT || !level) return 0;
    *level = gpio_input_bit_get(outputs[output].port, outputs[output].pin) != RESET;
    return 1;
}

static int read_input(void *context, ABoxBoardIoInput input, uint8_t *level)
{
    (void)context;
    if ((unsigned)input >= ABOX_V1_INPUT_COUNT || !level) return 0;
    *level = gpio_input_bit_get(GPIOA, PIN(ABOX_V1_INPUT_FIRST_PIN + (unsigned)input)) != RESET;
    return 1;
}

int ABoxBoardIo_ReadTerminal(ABoxBoardTerminal terminal, uint8_t *level)
{
    unsigned index = (unsigned)terminal - (unsigned)ABOX_BOARD_TERMINAL_B8;
    if (index >= ABOX_V1_INPUT_COUNT || !level) return 0;
    return read_input(0, (ABoxBoardIoInput)index, level);
}

static int write_led(void *context, ABoxBoardIoLed led, uint8_t level)
{
    (void)context;
    if ((unsigned)led >= ABOX_BOARD_IO_LED_COUNT) return 0;
    if (level) gpio_bit_set(leds[led].port, leds[led].pin);
    else gpio_bit_reset(leds[led].port, leds[led].pin);
    return 1;
}

static uint32_t enter_critical(void *context)
{ uint32_t key = __get_PRIMASK(); (void)context; __disable_irq(); return key; }
static void exit_critical(void *context, uint32_t key)
{ (void)context; __set_PRIMASK(key); }

const ABoxBoardIoPort *ABoxBoardIo_HardwarePort(void)
{
    static const ABoxBoardIoPort port = {
        0, prepare, write_output, read_output, read_input, write_led,
        enter_critical, exit_critical, prepare_leds, read_output_pin
    };
    return &port;
}
