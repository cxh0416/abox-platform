#include "abox_board_io_stm32f105.h"

#include "stm32f1xx_hal.h"
#include "../../hardware/boards/abox_v2/include/abox_v2_pins.h"

typedef struct {
    GPIO_TypeDef *gpio;
    uint16_t pin;
} BoardPin;

static const BoardPin outputs[ABOX_BOARD_IO_OUTPUT_COUNT] = {
    {GPIOA, GPIO_PIN_0}, {GPIOA, GPIO_PIN_1}, {GPIOA, GPIO_PIN_2}, {GPIOC, GPIO_PIN_4}
};
#if ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V1
static const BoardPin inputs[] = {
    {GPIOA, GPIO_PIN_4}, {GPIOA, GPIO_PIN_5},
    {GPIOA, GPIO_PIN_6}, {GPIOA, GPIO_PIN_7}
};
#define FIRST_INPUT_TERMINAL ABOX_BOARD_TERMINAL_B8
#else
static const BoardPin inputs[] = {
    ABOX_V2_INPUT_PINS
};
#define FIRST_INPUT_TERMINAL ABOX_BOARD_TERMINAL_B4
#endif
#define INPUT_COUNT (sizeof(inputs) / sizeof(inputs[0]))
static const BoardPin leds[ABOX_BOARD_IO_LED_COUNT] = {
    {GPIOB, GPIO_PIN_0}, {GPIOB, GPIO_PIN_1}, {GPIOB, GPIO_PIN_2}
};

void ABoxBoardIoStm32_SafeInit(void)
{
    GPIO_InitTypeDef config = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);

    config.Mode = GPIO_MODE_OUTPUT_PP;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW;
    config.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &config);
    config.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOC, &config);
    config.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
    HAL_GPIO_Init(GPIOB, &config);

    ABoxBoardIoStm32_InputsInit();
}

void ABoxBoardIoStm32_InputsInit(void)
{
    GPIO_InitTypeDef config = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    config.Mode = GPIO_MODE_INPUT;
    config.Pull = GPIO_NOPULL;
    config.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &config);
#if ABOX_BOARD_VERSION == ABOX_BOARD_VERSION_V2
    __HAL_RCC_GPIOC_CLK_ENABLE();
    config.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &config);
#endif
}

int ABoxBoardIoStm32_ReadTerminal(ABoxBoardTerminal terminal, uint8_t *level)
{
    unsigned index = (unsigned)terminal - FIRST_INPUT_TERMINAL;
    if (index >= INPUT_COUNT || !level) return 0;
    *level = HAL_GPIO_ReadPin(inputs[index].gpio, inputs[index].pin) == GPIO_PIN_SET ? 1U : 0U;
    return 1;
}

int ABoxBoardIo_ReadTerminal(ABoxBoardTerminal terminal, uint8_t *level)
{
    return ABoxBoardIoStm32_ReadTerminal(terminal, level);
}

static int prepare(void *context)
{
    (void)context;
    ABoxBoardIoStm32_SafeInit();
    return 1;
}

static int prepare_leds(void *context, uint8_t led_mask)
{
    GPIO_InitTypeDef config = {0};
    (void)context;
    if (!led_mask || (led_mask & (uint8_t)~7U)) return 0;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    config.Pin = led_mask; /* PB0/PB1/PB2 only; preserve all other GPIO. */
    HAL_GPIO_WritePin(GPIOB, config.Pin, GPIO_PIN_RESET);
    config.Mode = GPIO_MODE_OUTPUT_PP;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &config);
    return 1;
}

static int write_output(void *context, ABoxBoardIoOutput output, uint8_t level)
{
    (void)context;
    if ((unsigned)output >= ABOX_BOARD_IO_OUTPUT_COUNT) return 0;
    HAL_GPIO_WritePin(outputs[output].gpio, outputs[output].pin,
                      level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 1;
}

static int read_output(void *context, ABoxBoardIoOutput output, uint8_t *level)
{
    (void)context;
    if ((unsigned)output >= ABOX_BOARD_IO_OUTPUT_COUNT || !level) return 0;
    *level = (outputs[output].gpio->ODR & outputs[output].pin) ? 1U : 0U;
    return 1;
}

static int read_output_pin(void *context, ABoxBoardIoOutput output, uint8_t *level)
{
    (void)context;
    if ((unsigned)output >= ABOX_BOARD_IO_OUTPUT_COUNT || !level) return 0;
    *level = HAL_GPIO_ReadPin(outputs[output].gpio, outputs[output].pin) == GPIO_PIN_SET;
    return 1;
}

static int read_input(void *context, ABoxBoardIoInput input, uint8_t *level)
{
    (void)context;
    if ((unsigned)input >= INPUT_COUNT) return 0;
    return ABoxBoardIoStm32_ReadTerminal(
        (ABoxBoardTerminal)(FIRST_INPUT_TERMINAL + (unsigned)input), level);
}

static int write_led(void *context, ABoxBoardIoLed led, uint8_t level)
{
    (void)context;
    if ((unsigned)led >= ABOX_BOARD_IO_LED_COUNT) return 0;
    HAL_GPIO_WritePin(leds[led].gpio, leds[led].pin,
                      level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 1;
}

static uint32_t enter_critical(void *context)
{
    uint32_t key;
    (void)context;
    key = __get_PRIMASK();
    __disable_irq();
    return key;
}

static void exit_critical(void *context, uint32_t key)
{
    (void)context;
    __set_PRIMASK(key);
}

const ABoxBoardIoPort *ABoxBoardIoStm32_Port(void)
{
    static const ABoxBoardIoPort port = {
        0, prepare, write_output, read_output, read_input, write_led,
        enter_critical, exit_critical, prepare_leds, read_output_pin
    };
    return &port;
}

const ABoxBoardIoPort *ABoxBoardIo_HardwarePort(void)
{
    return ABoxBoardIoStm32_Port();
}
