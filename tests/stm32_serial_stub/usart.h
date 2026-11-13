#ifndef ABOX_TEST_USART_H
#define ABOX_TEST_USART_H
#include <stdint.h>
typedef struct { uint32_t BaudRate, Parity, WordLength, StopBits; } UART_InitTypeDef;
typedef struct { UART_InitTypeDef Init; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart5;
#define HAL_OK 0
#define UART_PARITY_NONE 0U
#define UART_WORDLENGTH_8B 8U
#define UART_STOPBITS_1 1U
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
int HAL_UART_DeInit(UART_HandleTypeDef *);
int HAL_UART_Init(UART_HandleTypeDef *);
int HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
int HAL_UART_AbortReceive(UART_HandleTypeDef *);
void test_flush_rx(UART_HandleTypeDef *);
#define __HAL_UART_FLUSH_DRREGISTER(uart) test_flush_rx(uart)
int HAL_UART_Transmit_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
int HAL_UART_AbortTransmit(UART_HandleTypeDef *);
int HAL_UART_Transmit(UART_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
#endif
