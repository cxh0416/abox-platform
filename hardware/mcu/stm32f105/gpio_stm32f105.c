#include "abox_gpio.h"
#include "main.h"

static GPIO_TypeDef *gpio_pin(ABoxGpioPin pin)
{
    static GPIO_TypeDef *const ports[] = {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE};
    if ((unsigned)pin.port >= sizeof(ports) / sizeof(ports[0]) || pin.index >= 16U) return 0;
    return ports[pin.port];
}
static void clock_enable(ABoxGpioPort port)
{
    switch (port) {
    case ABOX_GPIO_A: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case ABOX_GPIO_B: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case ABOX_GPIO_C: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case ABOX_GPIO_D: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
    case ABOX_GPIO_E: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
    }
}
static uint16_t locked(GPIO_TypeDef *port)
{
    uint32_t value = port->LCKR;
    return value & GPIO_LCKR_LCKK ? (uint16_t)value : 0U;
}

int ABoxGpio_Configure(ABoxGpioPin pin, const ABoxGpioConfig *config)
{
    GPIO_TypeDef *port = gpio_pin(pin);
    GPIO_InitTypeDef init = {0};
    if (!port || !config || (unsigned)config->mode > ABOX_GPIO_ALTERNATE_OPEN_DRAIN ||
        (unsigned)config->pull > ABOX_GPIO_PULL_DOWN ||
        (unsigned)config->speed > ABOX_GPIO_SPEED_50MHZ || config->initial_level > 1U ||
        (config->mode != ABOX_GPIO_INPUT && config->pull != ABOX_GPIO_NO_PULL)) return 0;
    switch (config->mode) {
    case ABOX_GPIO_INPUT: init.Mode = GPIO_MODE_INPUT; break;
    case ABOX_GPIO_ANALOG: init.Mode = GPIO_MODE_ANALOG; break;
    case ABOX_GPIO_OUTPUT_PUSH_PULL: init.Mode = GPIO_MODE_OUTPUT_PP; break;
    case ABOX_GPIO_OUTPUT_OPEN_DRAIN: init.Mode = GPIO_MODE_OUTPUT_OD; break;
    case ABOX_GPIO_ALTERNATE_PUSH_PULL: init.Mode = GPIO_MODE_AF_PP; break;
    default: init.Mode = GPIO_MODE_AF_OD; break;
    }
    init.Pin = 1UL << pin.index;
    init.Pull = config->pull == ABOX_GPIO_PULL_UP ? GPIO_PULLUP :
                config->pull == ABOX_GPIO_PULL_DOWN ? GPIO_PULLDOWN : GPIO_NOPULL;
    init.Speed = config->speed == ABOX_GPIO_SPEED_2MHZ ? GPIO_SPEED_FREQ_LOW :
                 config->speed == ABOX_GPIO_SPEED_10MHZ ? GPIO_SPEED_FREQ_MEDIUM : GPIO_SPEED_FREQ_HIGH;
    clock_enable(pin.port);
    if (locked(port) & init.Pin) return 0;
    if (config->mode >= ABOX_GPIO_OUTPUT_PUSH_PULL)
        HAL_GPIO_WritePin(port, (uint16_t)init.Pin, config->initial_level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_Init(port, &init);
    return 1;
}

int ABoxGpio_Write(ABoxGpioPin pin, uint8_t level)
{
    GPIO_TypeDef *port = gpio_pin(pin);
    if (!port || level > 1U) return 0;
    HAL_GPIO_WritePin(port, (uint16_t)(1UL << pin.index), level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 1;
}
int ABoxGpio_Read(ABoxGpioPin pin, uint8_t *level)
{
    GPIO_TypeDef *port = gpio_pin(pin);
    if (!port || !level) return 0;
    *level = HAL_GPIO_ReadPin(port, (uint16_t)(1UL << pin.index)) == GPIO_PIN_SET;
    return 1;
}
int ABoxGpio_ReadLatch(ABoxGpioPin pin, uint8_t *level)
{
    GPIO_TypeDef *port = gpio_pin(pin);
    if (!port || !level) return 0;
    *level = (port->ODR & (1UL << pin.index)) != 0U;
    return 1;
}
int ABoxGpio_Lock(ABoxGpioPort id, uint16_t pins)
{
    GPIO_TypeDef *port = gpio_pin((ABoxGpioPin){id, 0U});
    uint32_t state;
    if (!port || !pins) return 0;
    clock_enable(id); state = port->LCKR;
    if (state & GPIO_LCKR_LCKK) return (state & pins) == pins;
    if (HAL_GPIO_LockPin(port, pins) != HAL_OK) return 0;
    state = port->LCKR;
    return (state & GPIO_LCKR_LCKK) && (state & pins) == pins;
}
int ABoxGpio_GetLocked(ABoxGpioPort id, uint16_t *pins)
{
    GPIO_TypeDef *port = gpio_pin((ABoxGpioPin){id, 0U});
    if (!port || !pins) return 0;
    clock_enable(id); *pins = locked(port); return 1;
}
