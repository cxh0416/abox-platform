#ifndef GD32_UART_TEST_STUB_H
#define GD32_UART_TEST_STUB_H
#include <stdint.h>
typedef enum { RESET = 0, SET = 1 } FlagStatus;
#define GPIOC 2U
#define GPIOA 0U
#define GPIOD 3U
#define RCU_GPIOC 2U
#define RCU_GPIOD 3U
#define RCU_UART4 4U
#define RCU_GPIOA 0U
#define RCU_DMA0 5U
#define RCU_USART0 6U
#define USART0 0U
#define DMA0 0U
#define DMA_CH4 4U
#define USART0_IRQn 37
#define DMA0_Channel4_IRQn 15
#define UART4 4U
#define UART4_IRQn 53
#define GPIO_MODE_AF_PP 0x18U
#define GPIO_MODE_IN_FLOATING 0x04U
#define GPIO_OSPEED_50MHZ 3U
#define GPIO_OSPEED_2MHZ 2U
#define USART_WL_8BIT 0U
#define USART_STB_1BIT 0U
#define USART_PM_NONE 0U
#define USART_TRANSMIT_ENABLE 1U
#define USART_RECEIVE_ENABLE 1U
#define USART_STAT_PERR (1U << 0)
#define USART_STAT_FERR (1U << 1)
#define USART_STAT_NERR (1U << 2)
#define USART_STAT_ORERR (1U << 3)
#define USART_STAT_IDLEF (1U << 4)
#define USART_STAT_RBNE (1U << 5)
#define USART_STAT_TC (1U << 6)
#define USART_STAT_TBE (1U << 7)
#define USART_INT_RBNE 0U
#define USART_INT_IDLE 1U
#define USART_INT_ERR 2U
#define USART_INT_TBE 3U
#define USART_INT_TC 4U
#define USART_INT_FLAG_TBE USART_INT_TBE
#define USART_INT_FLAG_TC USART_INT_TC
#define USART_FLAG_TBE USART_STAT_TBE
#define USART_FLAG_TC USART_STAT_TC
#define USART_RECEIVE_DMA_ENABLE 1U
#define USART_RECEIVE_DMA_DISABLE 0U
#define DMA_INT_FLAG_G 1U
#define DMA_INT_FLAG_HTF 2U
#define DMA_INT_FLAG_FTF 4U
#define DMA_INT_FLAG_ERR 8U
#define DMA_INT_HTF DMA_INT_FLAG_HTF
#define DMA_INT_FTF DMA_INT_FLAG_FTF
#define DMA_INT_ERR DMA_INT_FLAG_ERR
#define DMA_FLAG_G DMA_INT_FLAG_G
#define DMA_PERIPH_INCREASE_DISABLE 0U
#define DMA_MEMORY_INCREASE_ENABLE 1U
#define DMA_PERIPHERAL_WIDTH_8BIT 0U
#define DMA_MEMORY_WIDTH_8BIT 0U
#define DMA_PERIPHERAL_TO_MEMORY 0U
#define DMA_PRIORITY_LOW 0U
typedef struct {
    uint32_t periph_addr, periph_width, memory_addr, memory_width, number, priority;
    uint8_t periph_inc, memory_inc, direction;
} dma_parameter_struct;
extern uint32_t test_usart_status;
extern uint32_t test_usart_data;
#define USART_STAT(uart) (test_usart_status)
#define USART_DATA(uart) (test_usart_data)
void rcu_periph_clock_enable(uint32_t port);
void gpio_init(uint32_t port, uint32_t mode, uint32_t speed, uint32_t pins);
void usart_deinit(uint32_t uart);
void usart_baudrate_set(uint32_t uart, uint32_t baud);
void usart_word_length_set(uint32_t uart, uint32_t word);
void usart_stop_bit_set(uint32_t uart, uint32_t stops);
void usart_parity_config(uint32_t uart, uint32_t parity);
void usart_transmit_config(uint32_t uart, uint32_t enabled);
void usart_receive_config(uint32_t uart, uint32_t enabled);
void usart_enable(uint32_t uart);
void NVIC_SetPriority(int irq, uint32_t priority);
void NVIC_EnableIRQ(int irq);
void usart_interrupt_enable(uint32_t uart, uint32_t flag);
void usart_interrupt_disable(uint32_t uart, uint32_t flag);
FlagStatus usart_interrupt_flag_get(uint32_t uart, uint32_t flag);
FlagStatus usart_flag_get(uint32_t uart, uint32_t flag);
uint16_t usart_data_receive(uint32_t uart);
void usart_data_transmit(uint32_t uart, uint16_t value);
void usart_dma_receive_config(uint32_t uart, uint8_t enable);
void dma_deinit(uint32_t dma, uint32_t channel);
void dma_struct_para_init(dma_parameter_struct *config);
void dma_init(uint32_t dma, uint32_t channel, dma_parameter_struct *config);
void dma_circulation_enable(uint32_t dma, uint32_t channel);
void dma_channel_enable(uint32_t dma, uint32_t channel);
void dma_channel_disable(uint32_t dma, uint32_t channel);
void dma_flag_clear(uint32_t dma, uint32_t channel, uint32_t flag);
void dma_interrupt_enable(uint32_t dma, uint32_t channel, uint32_t flag);
void dma_interrupt_disable(uint32_t dma, uint32_t channel, uint32_t flag);
FlagStatus dma_interrupt_flag_get(uint32_t dma, uint32_t channel, uint32_t flag);
void dma_interrupt_flag_clear(uint32_t dma, uint32_t channel, uint32_t flag);
uint32_t dma_transfer_number_get(uint32_t dma, uint32_t channel);
uint32_t test_get_primask(void);
void test_disable_irq(void);
void test_set_primask(uint32_t key);
#define __get_PRIMASK() test_get_primask()
#define __disable_irq() test_disable_irq()
#define __set_PRIMASK(key) test_set_primask(key)
#endif
