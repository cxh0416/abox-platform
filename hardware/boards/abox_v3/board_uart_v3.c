#include "abox_board_v3.h"
#include "gd32f10x.h"

static void enable_irq(IRQn_Type irq)
{
    NVIC_SetPriority(irq, 0U);
    NVIC_EnableIRQ(irq);
}

int ABoxBoardUart_Prepare(ABoxUartId id)
{
    uint32_t tx_port, rx_port;
    uint32_t tx_pin, rx_pin;
    IRQn_Type uart_irq;
    switch (id) {
    case ABOX_BOARD_UART_MODEM:
        rcu_periph_clock_enable(RCU_GPIOA);
        tx_port = rx_port = GPIOA;
        tx_pin = GPIO_PIN_9; rx_pin = GPIO_PIN_10;
        uart_irq = USART0_IRQn;
        enable_irq(DMA0_Channel3_IRQn);
        enable_irq(DMA0_Channel4_IRQn);
        break;
    case ABOX_BOARD_UART_RS232:
        rcu_periph_clock_enable(RCU_GPIOB);
        tx_port = rx_port = GPIOB;
        tx_pin = GPIO_PIN_10; rx_pin = GPIO_PIN_11;
        uart_irq = USART2_IRQn;
        enable_irq(DMA0_Channel1_IRQn);
        enable_irq(DMA0_Channel2_IRQn);
        break;
    case ABOX_BOARD_UART_TTL:
        rcu_periph_clock_enable(RCU_GPIOC);
        tx_port = rx_port = GPIOC;
        tx_pin = GPIO_PIN_10; rx_pin = GPIO_PIN_11;
        uart_irq = UART3_IRQn;
        enable_irq(DMA1_Channel2_IRQn);
        enable_irq(DMA1_Channel4_IRQn);
        break;
    case ABOX_BOARD_UART_RS485:
        rcu_periph_clock_enable(RCU_GPIOC);
        rcu_periph_clock_enable(RCU_GPIOD);
        gpio_bit_reset(GPIOC, GPIO_PIN_6);
        gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_6);
        tx_port = GPIOC; rx_port = GPIOD;
        tx_pin = GPIO_PIN_12; rx_pin = GPIO_PIN_2;
        uart_irq = UART4_IRQn;
        break;
    default:
        return 0;
    }

    gpio_init(tx_port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, tx_pin);
    gpio_init(rx_port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, rx_pin);
    enable_irq(uart_irq);
    return 1;
}

int ABoxBoardV3_Rs485Send(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    gpio_bit_set(GPIOC, GPIO_PIN_6);
    /* SendBlocking waits for TC, including the final stop bit. A timeout
     * aborts the frame and returns the transceiver to receive. */
    int sent = ABoxUart_SendBlocking(ABOX_BOARD_UART_RS485, data, length, timeout_ms);
    gpio_bit_reset(GPIOC, GPIO_PIN_6);
    return sent;
}
