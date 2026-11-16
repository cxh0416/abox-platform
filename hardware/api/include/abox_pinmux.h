#ifndef ABOX_PINMUX_H
#define ABOX_PINMUX_H

/* F105 alternate-function remapping. Route numbers are the hardware encoding:
 * UART1/2, SPI1/3, I2C1, TIMER4, CAN2, PD01, TIMER5_CH4 and TIMER2_ITR1 accept 0 or 1;
 * UART3 and TIMER1 accept 0, 1 or 3; TIMER2 accepts 0..3;
 * TIMER3 and CAN1 accept 0, 2 or 3. Route 0 is the reset mapping.
 * Actual pins depend on the device package and board; configure GPIO separately.
 * TIMER5_CH4 and TIMER2_ITR1 select internal signal routing, not GPIO pins. */
typedef enum {
    ABOX_PINMUX_SPI1, ABOX_PINMUX_I2C1, ABOX_PINMUX_UART1, ABOX_PINMUX_UART2,
    ABOX_PINMUX_UART3, ABOX_PINMUX_TIMER1, ABOX_PINMUX_TIMER2, ABOX_PINMUX_TIMER3,
    ABOX_PINMUX_TIMER4, ABOX_PINMUX_CAN1, ABOX_PINMUX_PD01,
    ABOX_PINMUX_TIMER5_CH4, ABOX_PINMUX_CAN2, ABOX_PINMUX_SPI3,
    ABOX_PINMUX_TIMER2_ITR1
} ABoxPinMuxSignal;
typedef enum {
    ABOX_PINMUX_ROUTE_0, ABOX_PINMUX_ROUTE_1, ABOX_PINMUX_ROUTE_2, ABOX_PINMUX_ROUTE_3
} ABoxPinMuxRoute;
typedef enum {
    ABOX_PINMUX_DEBUG_FULL, ABOX_PINMUX_DEBUG_NO_NJTRST,
    ABOX_PINMUX_DEBUG_SWD_ONLY, ABOX_PINMUX_DEBUG_DISABLED
} ABoxPinMuxDebug;

/* The F105 debug configuration field cannot be reliably read back. Every
 * operation explicitly sets the requested debug mode while preserving other
 * remaps. Disabling debug can make the device unreachable until reset.
 * Serialize all AFIO access, including vendor and board code; changing a
 * mapping does not arbitrate pin ownership or configure pins. */
int ABoxPinMux_Configure(ABoxPinMuxSignal signal, ABoxPinMuxRoute route, ABoxPinMuxDebug debug);
int ABoxPinMux_SetDebug(ABoxPinMuxDebug debug);
#endif
