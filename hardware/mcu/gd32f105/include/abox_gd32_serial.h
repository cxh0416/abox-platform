#ifndef ABOX_GD32_SERIAL_H
#define ABOX_GD32_SERIAL_H

/* Call from the GD32 UART4 IRQ (the board's STM32 UART5 equivalent). */
void ABoxGdSerial_IrqHandler(void);

#endif
