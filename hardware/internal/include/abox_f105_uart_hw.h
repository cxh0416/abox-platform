#ifndef ABOX_F105_UART_HW_H
#define ABOX_F105_UART_HW_H
#include <stdint.h>

/* F105 UART/DMA register layouts shared by the two supported vendors. */
typedef struct {
    volatile uint32_t status, data, baud, control1, control2, control3, guard;
} ABoxF105UartRegisters;
typedef struct {
    volatile uint32_t control, count, peripheral, memory;
} ABoxF105DmaChannel;
ABoxF105UartRegisters *ABoxF105Uart_Registers(unsigned id);
uint32_t ABoxF105Uart_ClockHz(unsigned id);
void ABoxF105Uart_EnableClock(unsigned id);
ABoxF105DmaChannel *ABoxF105Dma_Channel(unsigned controller, unsigned channel);
void ABoxF105Dma_EnableClock(unsigned controller);
/* Bits 1, 2, 3 denote complete, half, error. Clear uses write-one-to-clear. */
uint32_t ABoxF105Dma_Flags(unsigned controller, unsigned channel);
void ABoxF105Dma_Clear(unsigned controller, unsigned channel, uint32_t flags);
#endif
