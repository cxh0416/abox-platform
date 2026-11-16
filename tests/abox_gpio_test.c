#include <assert.h>
#include "abox_gpio.h"
#if defined(TEST_STM32)
#include "main.h"
GPIO_TypeDef test_gpio[5];
static unsigned clock_calls, writes, initializes, last_clock;
static GPIO_InitTypeDef last_config;
void test_gpio_clock(unsigned port) { ++clock_calls; last_clock = port; }
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{ assert(port == GPIOB && writes == 1U); ++initializes; last_config = *config; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState value)
{ ++writes; if (value) port->ODR |= pin; else port->ODR &= ~pin; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ (void)port; (void)pin; return GPIO_PIN_RESET; }
uint32_t HAL_GPIO_LockPin(GPIO_TypeDef *port, uint16_t pins)
{ port->LCKR = GPIO_LCKR_LCKK | pins; return HAL_OK; }
#else
#include "gd32f10x.h"
uint32_t test_gpio_lock[5];
static uint32_t latch[5];
static unsigned clock_calls, writes, initializes, last_clock, last_mode, last_speed;
void rcu_periph_clock_enable(uint32_t clock) { ++clock_calls; last_clock = clock - RCU_GPIOA; }
void gpio_bit_set(uint32_t port, uint32_t mask) { ++writes; latch[port] |= mask; }
void gpio_bit_reset(uint32_t port, uint32_t mask) { ++writes; latch[port] &= ~mask; }
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t mask)
{ assert(port == GPIOB && mask == (1U << 3U) && writes == 1U); ++initializes; last_mode = mode; last_speed = speed; }
uint32_t gpio_input_bit_get(uint32_t port, uint32_t mask) { (void)port; (void)mask; return RESET; }
uint32_t gpio_output_bit_get(uint32_t port, uint32_t mask) { return !!(latch[port] & mask); }
void gpio_pin_lock(uint32_t port, uint32_t mask) { GPIO_LOCK(port) = GPIO_LOCK_LKK | mask; }
#endif
int main(void)
{
    ABoxGpioPin pin = {ABOX_GPIO_B, 3U};
    ABoxGpioConfig config = {ABOX_GPIO_OUTPUT_OPEN_DRAIN, ABOX_GPIO_NO_PULL, ABOX_GPIO_SPEED_10MHZ, 1U};
    uint8_t level = 99U;
    assert(ABoxGpio_Configure(pin, &config));
    assert(clock_calls == 1U && last_clock == 1U && writes == 1U && initializes == 1U);
#if defined(TEST_STM32)
    assert(last_config.Pin == (1U << 3U) && last_config.Mode == GPIO_MODE_OUTPUT_OD && last_config.Speed == GPIO_SPEED_FREQ_MEDIUM);
#else
    assert(last_mode == GPIO_MODE_OUT_OD && last_speed == GPIO_OSPEED_10MHZ);
#endif
    assert(ABoxGpio_Read(pin, &level) && level == 0U);
    assert(ABoxGpio_ReadLatch(pin, &level) && level == 1U);
    assert(ABoxGpio_Write(pin, 0U) && ABoxGpio_ReadLatch(pin, &level) && level == 0U);
    config.pull = ABOX_GPIO_PULL_UP;
    assert(!ABoxGpio_Configure(pin, &config) && initializes == 1U && clock_calls == 1U);
    {
        uint16_t locked = 0xFFFFU;
        config.pull = ABOX_GPIO_NO_PULL;
        assert(!ABoxGpio_Lock(ABOX_GPIO_B, 0U));
        assert(ABoxGpio_GetLocked(ABOX_GPIO_B, &locked) && locked == 0U);
        assert(ABoxGpio_Lock(ABOX_GPIO_B, (1U << 3U) | (1U << 4U)));
        assert(ABoxGpio_GetLocked(ABOX_GPIO_B, &locked) && locked == ((1U << 3U) | (1U << 4U)));
        assert(ABoxGpio_Lock(ABOX_GPIO_B, 1U << 3U));
        assert(!ABoxGpio_Lock(ABOX_GPIO_B, 1U << 5U));
        assert(!ABoxGpio_Configure(pin, &config) && initializes == 1U && writes == 2U);
        assert(ABoxGpio_Write(pin, 1U) && ABoxGpio_ReadLatch(pin, &level) && level == 1U);
        locked = 0x1234U;
        assert(!ABoxGpio_GetLocked((ABoxGpioPort)99, &locked) && locked == 0x1234U);
    }
    pin.index = 16U; level = 42U;
    assert(!ABoxGpio_Read(pin, &level) && level == 42U);
    assert(!ABoxGpio_Write(pin, 1U) && !ABoxGpio_Configure(pin, &config));
    return 0;
}
