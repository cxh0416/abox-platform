#include "abox_gpio.h"
#include "gd32f10x.h"

static int gpio_pin(ABoxGpioPin pin, uint32_t *port, uint32_t *clock, uint32_t *mask)
{
    static const uint32_t ports[] = {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE};
    static const uint32_t clocks[] = {RCU_GPIOA, RCU_GPIOB, RCU_GPIOC, RCU_GPIOD, RCU_GPIOE};
    if ((unsigned)pin.port >= sizeof(ports) / sizeof(ports[0]) || pin.index >= 16U) return 0;
    *port = ports[pin.port]; *clock = clocks[pin.port]; *mask = 1UL << pin.index;
    return 1;
}

int ABoxGpio_Configure(ABoxGpioPin pin, const ABoxGpioConfig *config)
{
    uint32_t port, clock, mask, mode, speed;
    if (!config || !gpio_pin(pin, &port, &clock, &mask) ||
        (unsigned)config->mode > ABOX_GPIO_ALTERNATE_OPEN_DRAIN ||
        (unsigned)config->pull > ABOX_GPIO_PULL_DOWN ||
        (unsigned)config->speed > ABOX_GPIO_SPEED_50MHZ || config->initial_level > 1U ||
        (config->mode != ABOX_GPIO_INPUT && config->pull != ABOX_GPIO_NO_PULL)) return 0;
    switch (config->mode) {
    case ABOX_GPIO_INPUT:
        mode = config->pull == ABOX_GPIO_PULL_UP ? GPIO_MODE_IPU :
               config->pull == ABOX_GPIO_PULL_DOWN ? GPIO_MODE_IPD : GPIO_MODE_IN_FLOATING; break;
    case ABOX_GPIO_ANALOG: mode = GPIO_MODE_AIN; break;
    case ABOX_GPIO_OUTPUT_PUSH_PULL: mode = GPIO_MODE_OUT_PP; break;
    case ABOX_GPIO_OUTPUT_OPEN_DRAIN: mode = GPIO_MODE_OUT_OD; break;
    case ABOX_GPIO_ALTERNATE_PUSH_PULL: mode = GPIO_MODE_AF_PP; break;
    default: mode = GPIO_MODE_AF_OD; break;
    }
    speed = config->speed == ABOX_GPIO_SPEED_2MHZ ? GPIO_OSPEED_2MHZ :
            config->speed == ABOX_GPIO_SPEED_10MHZ ? GPIO_OSPEED_10MHZ : GPIO_OSPEED_50MHZ;
    rcu_periph_clock_enable(clock);
    if ((GPIO_LOCK(port) & GPIO_LOCK_LKK) && (GPIO_LOCK(port) & mask)) return 0;
    if (config->mode >= ABOX_GPIO_OUTPUT_PUSH_PULL) {
        if (config->initial_level) gpio_bit_set(port, mask); else gpio_bit_reset(port, mask);
    }
    gpio_init(port, mode, speed, mask);
    return 1;
}

int ABoxGpio_Write(ABoxGpioPin pin, uint8_t level)
{
    uint32_t port, clock, mask;
    if (level > 1U || !gpio_pin(pin, &port, &clock, &mask)) return 0;
    if (level) gpio_bit_set(port, mask); else gpio_bit_reset(port, mask);
    return 1;
}
int ABoxGpio_Read(ABoxGpioPin pin, uint8_t *level)
{
    uint32_t port, clock, mask;
    if (!level || !gpio_pin(pin, &port, &clock, &mask)) return 0;
    *level = gpio_input_bit_get(port, mask) != RESET;
    return 1;
}
int ABoxGpio_ReadLatch(ABoxGpioPin pin, uint8_t *level)
{
    uint32_t port, clock, mask;
    if (!level || !gpio_pin(pin, &port, &clock, &mask)) return 0;
    *level = gpio_output_bit_get(port, mask) != RESET;
    return 1;
}
int ABoxGpio_Lock(ABoxGpioPort id, uint16_t pins)
{
    uint32_t port, clock, mask, state;
    if (!pins || !gpio_pin((ABoxGpioPin){id, 0U}, &port, &clock, &mask)) return 0;
    rcu_periph_clock_enable(clock); state = GPIO_LOCK(port);
    if (state & GPIO_LOCK_LKK) return (state & pins) == pins;
    gpio_pin_lock(port, pins); state = GPIO_LOCK(port);
    return (state & GPIO_LOCK_LKK) && (state & pins) == pins;
}
int ABoxGpio_GetLocked(ABoxGpioPort id, uint16_t *pins)
{
    uint32_t port, clock, mask, state;
    if (!pins || !gpio_pin((ABoxGpioPin){id, 0U}, &port, &clock, &mask)) return 0;
    rcu_periph_clock_enable(clock); state = GPIO_LOCK(port);
    *pins = state & GPIO_LOCK_LKK ? (uint16_t)state : 0U; return 1;
}
