#include "abox_serial_port.h"
#include "abox_gd32_serial.h"
#include "abox_platform_port.h"
#include "gd32f10x.h"

#define SERIAL_UART UART4
#define SERIAL_TX_PIN (1UL << 12)
#define SERIAL_RX_PIN (1UL << 2)
#define SERIAL_ERRORS (USART_STAT_PERR | USART_STAT_FERR | USART_STAT_NERR | USART_STAT_ORERR)

static uint32_t g_baud = 115200U;
static uint8_t *g_rx;
static uint16_t g_rx_size, g_rx_pos;
static const uint8_t *g_tx;
static uint16_t g_tx_size, g_tx_pos;
static volatile uint8_t g_rx_active, g_tx_active;

static uint32_t enter_critical(void)
{ uint32_t key = __get_PRIMASK(); __disable_irq(); return key; }
static void leave_critical(uint32_t key) { __set_PRIMASK(key); }

int ABoxSerialPort_IsConfig(uint32_t baud) { return g_baud == baud; }
void ABoxSerialPort_SetConfig(uint32_t baud) { g_baud = baud; }

int ABoxSerialPort_Init(void)
{
    uint32_t clock = rcu_clock_freq_get(CK_APB1);
    uint64_t divisor;
    if (!g_baud || !clock || g_rx_active || g_tx_active) return 0;
    /* UART4 uses APB1 and 16x oversampling. Reject unrepresentable baud
     * rates before the vendor helper divides or truncates the divisor. */
    divisor = ((uint64_t)clock + g_baud / 2U) / g_baud;
    if (divisor < 16U || divisor > 65535U) return 0;
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_UART4);
    gpio_init(GPIOC, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, SERIAL_TX_PIN);
    gpio_init(GPIOD, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, SERIAL_RX_PIN);
    usart_deinit(SERIAL_UART);
    usart_baudrate_set(SERIAL_UART, g_baud);
    usart_word_length_set(SERIAL_UART, USART_WL_8BIT);
    usart_stop_bit_set(SERIAL_UART, USART_STB_1BIT);
    usart_parity_config(SERIAL_UART, USART_PM_NONE);
    usart_transmit_config(SERIAL_UART, USART_TRANSMIT_ENABLE);
    usart_receive_config(SERIAL_UART, USART_RECEIVE_ENABLE);
    usart_enable(SERIAL_UART);
    NVIC_SetPriority(UART4_IRQn, 0U);
    NVIC_EnableIRQ(UART4_IRQn);
    return 1;
}

int ABoxSerialPort_Deinit(void)
{
    ABoxSerialPort_AbortRx();
    ABoxSerialPort_AbortTx();
    usart_deinit(SERIAL_UART);
    return 1;
}

int ABoxSerialPort_StartRx(uint8_t *data, uint16_t length)
{
    uint32_t key;
    if (!data || !length) return 0;
    key = enter_critical();
    if (g_rx_active) { leave_critical(key); return 0; }
    g_rx = data; g_rx_size = length; g_rx_pos = 0U; g_rx_active = 1U;
    usart_interrupt_enable(SERIAL_UART, USART_INT_RBNE);
    usart_interrupt_enable(SERIAL_UART, USART_INT_IDLE);
    usart_interrupt_enable(SERIAL_UART, USART_INT_ERR);
    leave_critical(key);
    return 1;
}

int ABoxSerialPort_AbortRx(void)
{
    uint32_t key = enter_critical();
    usart_interrupt_disable(SERIAL_UART, USART_INT_RBNE);
    usart_interrupt_disable(SERIAL_UART, USART_INT_IDLE);
    usart_interrupt_disable(SERIAL_UART, USART_INT_ERR);
    g_rx_active = 0U;
    leave_critical(key);
    return 1;
}

int ABoxSerialPort_Send(const uint8_t *data, uint16_t length)
{
    uint32_t key;
    if (!data || !length) return 0;
    key = enter_critical();
    if (g_tx_active) { leave_critical(key); return 0; }
    g_tx = data; g_tx_size = length; g_tx_pos = 0U; g_tx_active = 1U;
    usart_interrupt_enable(SERIAL_UART, USART_INT_TBE);
    leave_critical(key);
    return 1;
}

void ABoxSerialPort_AbortTx(void)
{
    uint32_t key = enter_critical();
    usart_interrupt_disable(SERIAL_UART, USART_INT_TBE);
    usart_interrupt_disable(SERIAL_UART, USART_INT_TC);
    g_tx_active = 0U;
    leave_critical(key);
}

int ABoxSerialPort_SendBlocking(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    uint32_t start = ABox_PortGetTickMs();
    uint32_t key;
    if (!data || !length) return 0;
    key = enter_critical();
    if (g_tx_active) { leave_critical(key); return 0; }
    g_tx_active = 1U;
    leave_critical(key);
    for (uint16_t i = 0U; i < length; ++i) {
        while (usart_flag_get(SERIAL_UART, USART_FLAG_TBE) == RESET)
            if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout_ms) {
                ABoxSerialPort_AbortTx();
                return 0;
            }
        usart_data_transmit(SERIAL_UART, data[i]);
    }
    while (usart_flag_get(SERIAL_UART, USART_FLAG_TC) == RESET)
        if ((uint32_t)(ABox_PortGetTickMs() - start) >= timeout_ms) {
            ABoxSerialPort_AbortTx();
            return 0;
        }
    ABoxSerialPort_AbortTx();
    return 1;
}

void ABoxGdSerial_IrqHandler(void)
{
    uint32_t status = USART_STAT(SERIAL_UART);
    uint8_t delivered = 0U;

    /* Reading STAT then DATA clears IDLE/errors; capture RBNE before DATA. */
    if (g_rx_active && (status & (SERIAL_ERRORS | USART_STAT_RBNE | USART_STAT_IDLEF))) {
        uint8_t byte = (uint8_t)usart_data_receive(SERIAL_UART);
        if (status & SERIAL_ERRORS) {
            (void)ABoxSerialPort_AbortRx();
            ABoxSerialPort_NotifyError();
            return;
        }
        if (status & USART_STAT_RBNE) {
            g_rx[g_rx_pos++] = byte;
            if (g_rx_pos == g_rx_size) {
                uint16_t count = g_rx_pos;
                uint8_t *buffer = g_rx;
                (void)ABoxSerialPort_AbortRx();
                delivered = 1U;
                ABoxSerialPort_NotifyRx(buffer, count);
            }
        }
        if (!delivered && (status & USART_STAT_IDLEF) && g_rx_pos) {
            uint16_t count = g_rx_pos;
            uint8_t *buffer = g_rx;
            (void)ABoxSerialPort_AbortRx();
            ABoxSerialPort_NotifyRx(buffer, count);
        }
    }
    if (g_tx_active && usart_interrupt_flag_get(SERIAL_UART, USART_INT_FLAG_TBE) != RESET) {
        usart_data_transmit(SERIAL_UART, g_tx[g_tx_pos++]);
        if (g_tx_pos == g_tx_size) {
            usart_interrupt_disable(SERIAL_UART, USART_INT_TBE);
            usart_interrupt_enable(SERIAL_UART, USART_INT_TC);
        }
    }
    if (g_tx_active && usart_interrupt_flag_get(SERIAL_UART, USART_INT_FLAG_TC) != RESET) {
        usart_interrupt_disable(SERIAL_UART, USART_INT_TC);
        g_tx_active = 0U;
        ABoxSerialPort_NotifyTxComplete();
    }
}
