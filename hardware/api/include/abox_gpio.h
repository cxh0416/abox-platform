#ifndef ABOX_GPIO_H
#define ABOX_GPIO_H
#include <stdint.h>

typedef enum { ABOX_GPIO_A, ABOX_GPIO_B, ABOX_GPIO_C, ABOX_GPIO_D, ABOX_GPIO_E } ABoxGpioPort;
typedef struct { ABoxGpioPort port; uint8_t index; } ABoxGpioPin;
typedef enum {
    ABOX_GPIO_INPUT, ABOX_GPIO_ANALOG, ABOX_GPIO_OUTPUT_PUSH_PULL,
    ABOX_GPIO_OUTPUT_OPEN_DRAIN, ABOX_GPIO_ALTERNATE_PUSH_PULL, ABOX_GPIO_ALTERNATE_OPEN_DRAIN
} ABoxGpioMode;
typedef enum { ABOX_GPIO_NO_PULL, ABOX_GPIO_PULL_UP, ABOX_GPIO_PULL_DOWN } ABoxGpioPull;
typedef enum { ABOX_GPIO_SPEED_2MHZ, ABOX_GPIO_SPEED_10MHZ, ABOX_GPIO_SPEED_50MHZ } ABoxGpioSpeed;
typedef struct {
    ABoxGpioMode mode;
    ABoxGpioPull pull;
    ABoxGpioSpeed speed;
    uint8_t initial_level;
} ABoxGpioConfig;

/* Physical MCU GPIO. Board configuration must supply bonded, available pins;
 * callers own pin multiplexing and must not reconfigure a shared peripheral
 * or board safety output. Alternate-function remapping is configured separately.
 * Configure validates the entire request before writing a latch or registers.
 * For outputs, the initial latch is written before enabling output mode. */
int ABoxGpio_Configure(ABoxGpioPin pin, const ABoxGpioConfig *config);
int ABoxGpio_Write(ABoxGpioPin pin, uint8_t level);
/* Input pin and output latch are distinct; neither verifies an external load.
 * Failed reads do not modify the output pointer. */
int ABoxGpio_Read(ABoxGpioPin pin, uint8_t *level);
int ABoxGpio_ReadLatch(ABoxGpioPin pin, uint8_t *level);
/* Lock the configuration of all selected pins of a single port until MCU
 * reset. F105 locks each port only once: include every desired pin in the
 * same mask. An already-locked subset succeeds; adding pins fails. This locks
 * configuration, not output writes. GetLocked returns a 16-bit mask; failed
 * reads leave it unchanged. The board must serialize GPIO and vendor users. */
int ABoxGpio_Lock(ABoxGpioPort port, uint16_t pins);
int ABoxGpio_GetLocked(ABoxGpioPort port, uint16_t *pins);
#endif
