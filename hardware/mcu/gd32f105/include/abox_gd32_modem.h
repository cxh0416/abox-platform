#ifndef ABOX_GD32_MODEM_H
#define ABOX_GD32_MODEM_H

/* Call Init once before binding the platform port; IRQ entry points are
 * connected by the selected target's startup/interrupt implementation. */
int ABoxGdModem_Init(void);
void ABoxGdModem_UsartIrq(void);
void ABoxGdModem_DmaIrq(void);

#endif
