#ifndef ABOX_STM32F105_EC800_PORT_H
#define ABOX_STM32F105_EC800_PORT_H

/* Bind the standard App Flash, UART1, EC800 power and time callbacks. */
void ABoxStm32F105Ec800Port_Init(void);
int ABoxStm32F105Ec800Port_TryInit(void);

#endif
