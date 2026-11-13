#ifndef ABOX_BOARD_TEST_USART_H
#define ABOX_BOARD_TEST_USART_H
#include <stdint.h>
#define USART1 ((void *)0x101U)
#define UART5 ((void *)0x105U)
typedef struct { void *Instance; uint32_t ErrorCode; } UART_HandleTypeDef;
void MX_USART1_UART_Init(void);
void MX_UART5_Init(void);
uint32_t HAL_UART_GetState(UART_HandleTypeDef *);
#endif
