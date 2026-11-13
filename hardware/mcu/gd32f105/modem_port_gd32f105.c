#include "abox_modem_port.h"
#include "abox_gd32_modem.h"
#include "abox_platform_port.h"
#include "gd32f10x.h"

#define MODEM_UART USART0
#define MODEM_DMA DMA0
#define MODEM_CHANNEL DMA_CH4
#define MODEM_TX_PIN (1UL << 9)
#define MODEM_RX_PIN (1UL << 10)
#define MODEM_ERRORS (USART_STAT_PERR | USART_STAT_FERR | USART_STAT_NERR | USART_STAT_ORERR)

static uint16_t g_capacity, g_last_position;
static uint8_t g_configured, g_rx_active, g_expect_full;

int ABoxGdModem_Init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_USART0);
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, MODEM_TX_PIN);
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, MODEM_RX_PIN);
    usart_deinit(MODEM_UART);
    usart_baudrate_set(MODEM_UART, 115200U);
    usart_word_length_set(MODEM_UART, USART_WL_8BIT);
    usart_stop_bit_set(MODEM_UART, USART_STB_1BIT);
    usart_parity_config(MODEM_UART, USART_PM_NONE);
    usart_transmit_config(MODEM_UART, USART_TRANSMIT_ENABLE);
    usart_receive_config(MODEM_UART, USART_RECEIVE_ENABLE);
    usart_enable(MODEM_UART);
    NVIC_SetPriority(USART0_IRQn, 0U);
    NVIC_SetPriority(DMA0_Channel4_IRQn, 0U);
    NVIC_EnableIRQ(USART0_IRQn);
    NVIC_EnableIRQ(DMA0_Channel4_IRQn);
    g_configured = 1U;
    return 1;
}

int ABoxModemPort_AbortRx(void)
{
    uint32_t key = __get_PRIMASK();
    if (!g_configured) return 0;
    __disable_irq();
    g_rx_active = 0U;
    usart_interrupt_disable(MODEM_UART, USART_INT_IDLE);
    usart_interrupt_disable(MODEM_UART, USART_INT_ERR);
    usart_dma_receive_config(MODEM_UART, USART_RECEIVE_DMA_DISABLE);
    dma_channel_disable(MODEM_DMA, MODEM_CHANNEL);
    dma_interrupt_disable(MODEM_DMA, MODEM_CHANNEL, DMA_INT_HTF | DMA_INT_FTF | DMA_INT_ERR);
    dma_flag_clear(MODEM_DMA, MODEM_CHANNEL, DMA_FLAG_G);
    __set_PRIMASK(key);
    return 1;
}

int ABoxModemPort_StartRx(uint8_t *buffer, uint16_t capacity)
{
    dma_parameter_struct config;
    uint32_t key;
    if (!g_configured || !buffer || capacity < 2U || (capacity & 1U)) return 0;
    key = __get_PRIMASK();
    __disable_irq();
    if (g_rx_active) { __set_PRIMASK(key); return 0; }
    dma_deinit(MODEM_DMA, MODEM_CHANNEL);
    dma_struct_para_init(&config);
    config.periph_addr = (uint32_t)(uintptr_t)&USART_DATA(MODEM_UART);
    config.memory_addr = (uint32_t)(uintptr_t)buffer;
    config.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    config.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    config.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
    config.memory_width = DMA_MEMORY_WIDTH_8BIT;
    config.direction = DMA_PERIPHERAL_TO_MEMORY;
    config.number = capacity;
    config.priority = DMA_PRIORITY_LOW;
    dma_init(MODEM_DMA, MODEM_CHANNEL, &config);
    dma_circulation_enable(MODEM_DMA, MODEM_CHANNEL);
    dma_flag_clear(MODEM_DMA, MODEM_CHANNEL, DMA_FLAG_G);
    dma_interrupt_enable(MODEM_DMA, MODEM_CHANNEL, DMA_INT_HTF | DMA_INT_FTF | DMA_INT_ERR);
    g_capacity = capacity; g_last_position = 0U; g_expect_full = 0U; g_rx_active = 1U;
    dma_channel_enable(MODEM_DMA, MODEM_CHANNEL);
    usart_dma_receive_config(MODEM_UART, USART_RECEIVE_DMA_ENABLE);
    usart_interrupt_enable(MODEM_UART, USART_INT_IDLE);
    usart_interrupt_enable(MODEM_UART, USART_INT_ERR);
    __set_PRIMASK(key);
    return 1;
}

int ABoxModemPort_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    uint32_t start = ABox_PortGetTickMs();
    if (!g_configured || !data || !length) return 0;
    for (uint16_t i = 0U; i < length; ++i) {
        while (usart_flag_get(MODEM_UART, USART_FLAG_TBE) == RESET)
            if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout_ms) return 0;
        usart_data_transmit(MODEM_UART, data[i]);
    }
    while (usart_flag_get(MODEM_UART, USART_FLAG_TC) == RESET)
        if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout_ms) return 0;
    return 1;
}

static int dma_pending(void)
{
    int half, full;
    if (dma_interrupt_flag_get(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_ERR) != RESET) {
        (void)ABoxModemPort_AbortRx(); ABoxModemPort_NotifyError(); return 0;
    }
    half = dma_interrupt_flag_get(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_HTF) != RESET;
    full = dma_interrupt_flag_get(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_FTF) != RESET;
    if (half && full && !g_expect_full &&
        dma_transfer_number_get(MODEM_DMA, MODEM_CHANNEL) <= g_capacity / 2U) {
        (void)ABoxModemPort_AbortRx(); ABoxModemPort_NotifyError(); return 0;
    }
    if (half) {
        dma_interrupt_flag_clear(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_HTF);
        if (g_expect_full || g_last_position > g_capacity / 2U) {
            (void)ABoxModemPort_AbortRx(); ABoxModemPort_NotifyError(); return 0;
        }
        g_expect_full = 1U; g_last_position = g_capacity / 2U;
        ABoxModemPort_NotifyRx(g_last_position);
    }
    if (full) {
        dma_interrupt_flag_clear(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_FTF);
        if (!g_expect_full) {
            (void)ABoxModemPort_AbortRx(); ABoxModemPort_NotifyError(); return 0;
        }
        g_expect_full = 0U; g_last_position = 0U;
        ABoxModemPort_NotifyRx(g_capacity);
    }
    return 1;
}

void ABoxGdModem_DmaIrq(void)
{ if (g_rx_active) (void)dma_pending(); }

void ABoxGdModem_UsartIrq(void)
{
    uint32_t status, remaining;
    unsigned attempt;
    if (!g_rx_active) return;
    status = USART_STAT(MODEM_UART);
    if (!(status & (MODEM_ERRORS | USART_STAT_IDLEF))) return;
    (void)usart_data_receive(MODEM_UART); /* STAT followed by DATA clears flags. */
    if (status & MODEM_ERRORS) {
        (void)ABoxModemPort_AbortRx(); ABoxModemPort_NotifyError(); return;
    }
    /* A boundary may arrive during the counter read. Only publish a sample
     * after a subsequent flag check proves it preceded the next boundary. */
    for (attempt = 0U; attempt < 3U; ++attempt) {
        if (!dma_pending()) return;
        remaining = dma_transfer_number_get(MODEM_DMA, MODEM_CHANNEL);
        if (dma_interrupt_flag_get(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_HTF) == RESET &&
            dma_interrupt_flag_get(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_FTF) == RESET &&
            dma_interrupt_flag_get(MODEM_DMA, MODEM_CHANNEL, DMA_INT_FLAG_ERR) == RESET) break;
    }
    if (attempt == 3U) {
        (void)ABoxModemPort_AbortRx(); ABoxModemPort_NotifyError(); return;
    }
    if (remaining == 0U || remaining >= g_capacity) return;
    uint16_t position = (uint16_t)(g_capacity - remaining);
    if (position < g_last_position) {
        (void)ABoxModemPort_AbortRx(); ABoxModemPort_NotifyError(); return;
    }
    if (position > g_last_position) {
        g_last_position = position;
        ABoxModemPort_NotifyRx(position);
    }
}
