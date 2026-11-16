#ifndef ABOX_MCU_CLOCK_H
#define ABOX_MCU_CLOCK_H
#include <stdint.h>
#include "abox_hal_status.h"

typedef enum { ABOX_MCU_CLOCK_HSI, ABOX_MCU_CLOCK_HSE, ABOX_MCU_CLOCK_PLL_HSI,
               ABOX_MCU_CLOCK_PLL_HSE, ABOX_MCU_CLOCK_HSE_BYPASS,
               ABOX_MCU_CLOCK_PLL_HSE_BYPASS, ABOX_MCU_CLOCK_PLL_AUX_HSE,
               ABOX_MCU_CLOCK_PLL_AUX_HSE_BYPASS } ABoxMcuClockSource;
typedef enum { ABOX_MCU_CLOCK_STM32F105, ABOX_MCU_CLOCK_GD32F105 } ABoxMcuClockVariant;
/* ST PLL2/PLL3 correspond to GD PLL1/PLL2 respectively. */
typedef enum {
    ABOX_MCU_CLOCK_OUT_OFF=0, ABOX_MCU_CLOCK_OUT_SYSTEM=4,
    ABOX_MCU_CLOCK_OUT_HSI=5, ABOX_MCU_CLOCK_OUT_HSE=6,
    ABOX_MCU_CLOCK_OUT_MAIN_PLL_DIV2=7, ABOX_MCU_CLOCK_OUT_AUX_PLL=8,
    ABOX_MCU_CLOCK_OUT_I2S_PLL_DIV2=9, ABOX_MCU_CLOCK_OUT_I2S_PLL=11
} ABoxMcuClockOutput;
/* Encoded multiplier for the F105 connectivity-line PLL's 6.5 setting. */
#define ABOX_MCU_CLOCK_PLL_MUL_6_5 65U
typedef struct {
    ABoxMcuClockSource source;
    uint32_t external_hz; /* actual crystal or external clock frequency */
    uint8_t external_divider; /* PLL HSE input / 1..16 */
    uint8_t pll_multiplier;   /* integer multiplier, or ABOX_MCU_CLOCK_PLL_MUL_6_5 */
    uint16_t ahb_divider;     /* 1,2,4,8,16,64,128,256,512 */
    uint8_t apb1_divider, apb2_divider; /* 1,2,4,8,16 */
    uint8_t aux_divider, aux_multiplier; /* only PLL_AUX: HSE / 1..16 * factor */
} ABoxMcuClockConfig;
typedef struct {
    uint32_t system_hz, ahb_hz, apb1_hz, apb2_hz, apb1_timer_hz, apb2_timer_hz;
} ABoxMcuClockFrequencies;

/* Pure validation/calculation using the selected target's PLL and bus limits.
 * CalculateFor permits checking either target without touching hardware.
 * STM32F105: x4..x9 and x6.5, SYSCLK <=72 MHz, APB1 <=36 MHz.
 * GD32F105: x2..x14, x16..x32 and x6.5, SYSCLK <=108 MHz, APB1 <=54 MHz.
 * HSE_BYPASS sources select an external clock signal instead of a crystal.
 * PLL_AUX sources feed the main PLL from ST PLL2 / GD PLL1; external_divider
 * divides the auxiliary PLL output before it enters the main PLL. The auxiliary
 * fields are ignored by all other sources and must be set for PLL_AUX.
 * Calculated frequencies are rounded down to whole Hz after PLL multiplication.
 * The actual HSE must match the board source and the selected vendor
 * library's HSE_VALUE/HXTAL_VALUE build constant. */
ABoxHalStatus ABoxMcuClock_CalculateFor(ABoxMcuClockVariant variant,const ABoxMcuClockConfig *config,
                                        ABoxMcuClockFrequencies *out);
ABoxHalStatus ABoxMcuClock_Calculate(const ABoxMcuClockConfig *config, ABoxMcuClockFrequencies *out);
/* Startup-only: call with HSI as the current clock, main PLL disabled, and
 * all dependent peripheral owners stopped. If HSE is already enabled in a
 * different bypass mode, Apply returns BUSY instead of switching it live. A
 * PLL_AUX configuration requires the auxiliary PLL stopped and its shared
 * input divider compatible with any running I2S PLL.
 * The STM32 HAL tick must already
 * advance for oscillator timeouts. Failure may leave a partially changed
 * clock tree; stop startup and perform board-specific recovery/reset. */
ABoxHalStatus ABoxMcuClock_Apply(const ABoxMcuClockConfig *config);
ABoxHalStatus ABoxMcuClock_Get(ABoxMcuClockFrequencies *out);
/* Select the hardware clock output; the caller configures its GPIO and keeps
 * the chosen oscillator/PLL running. OUT_OFF disables the output selection. */
ABoxHalStatus ABoxMcuClock_SetOutput(ABoxMcuClockOutput source);
ABoxHalStatus ABoxMcuClock_GetOutput(ABoxMcuClockOutput *source);
/* HSE failure monitoring raises NMI on F105. The board NMI handler checks and
 * clears the flag and initiates its own clock/peripheral recovery. A clock
 * fallback changes real peripheral timings; it does not reconfigure them.
 * Serialize these calls and clock-output configuration with other RCC users. */
ABoxHalStatus ABoxMcuClock_SetFailureMonitor(uint8_t enabled);
ABoxHalStatus ABoxMcuClock_FailurePending(uint8_t *pending);
void ABoxMcuClock_ClearFailure(void);
#endif
