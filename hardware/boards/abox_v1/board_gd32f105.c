#include "abox_gd32_v1.h"
#include "abox_board.h"
#include "abox_board_io.h"
#include "abox_gd32f105_flash.h"
#include "abox_gd32_modem.h"
#include "abox_gd32_monotonic.h"
#include "abox_modem_port.h"
#include "abox_platform_port.h"
#include "abox_serial_port.h"
#include "boot_cfg.h"
#include "ec_power.h"
#include "gd32f10x.h"

uint32_t SystemCoreClock = 8000000U;

static int configure_clock(void)
{
    uint32_t timeout;
    /* Frozen Boot deinitializes its clock before jumping; do not rewrite a
     * PLL while it still clocks the CPU if a different entry path is used. */
    if (rcu_system_clock_source_get() != RCU_SCSS_IRC8M ||
        rcu_flag_get(RCU_FLAG_PLLSTB) != RESET) return 0;
    rcu_osci_on(RCU_HXTAL);
    if (rcu_osci_stab_wait(RCU_HXTAL) != SUCCESS) return 0;
    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV2);
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV1);
    rcu_predv0_config(RCU_PREDV0SRC_HXTAL, RCU_PREDV0_DIV1);
    rcu_pll_config(RCU_PLLSRC_HXTAL, RCU_PLL_MUL9);
    rcu_osci_on(RCU_PLL_CK);
    if (rcu_osci_stab_wait(RCU_PLL_CK) != SUCCESS) return 0;
    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    for (timeout = 0U; timeout < 100000U; ++timeout)
        if (rcu_system_clock_source_get() == RCU_SCSS_PLL) break;
    if (timeout == 100000U) return 0;
    SystemCoreClock = rcu_clock_freq_get(CK_AHB);
    return SystemCoreClock == 72000000U && rcu_clock_freq_get(CK_APB1) == 36000000U;
}

static uint32_t tick(void *context) { (void)context; return (uint32_t)ABoxGdMonotonic_GetMs(); }
static uint64_t monotonic(void *context) { (void)context; return ABoxGdMonotonic_GetMs(); }
static int uart_write(void *context, const uint8_t *data, uint32_t length)
{ (void)context; return length && length <= 65535U && ABoxModemPort_Write(data, (uint16_t)length, 1000U); }
static void critical_enter(void *context) { (void)context; __disable_irq(); }
static void critical_leave(void *context) { (void)context; __enable_irq(); }
static void power_write(void *context, uint8_t level)
{ (void)context; if (level) gpio_bit_set(GPIOC, 1U << 7); else gpio_bit_reset(GPIOC, 1U << 7); }
static void pwrkey_write(void *context, uint8_t level)
{ (void)context; if (level) gpio_bit_set(GPIOC, 1U << 8); else gpio_bit_reset(GPIOC, 1U << 8); }

int ABoxGdV1_Init(void)
{
    static const ABoxFlashLayout layout = {APP_START_ADDR, OTA_INFO_ADDR, OTA_FLASH_PAGE_SIZE};
    static const ABoxPlatformPort port = {
        .get_tick_ms = tick, .uart_write = uart_write,
        .flash_begin = ABoxGd32Flash_Begin, .flash_write = ABoxGd32Flash_Write,
        .flash_erase_page = ABoxGd32Flash_ErasePage, .flash_end = ABoxGd32Flash_End,
        .enter_critical = critical_enter, .exit_critical = critical_leave,
        .flash_layout = &layout, .ec_power_enable_write = power_write,
        .ec_pwrkey_write = pwrkey_write, .get_monotonic_ms = monotonic
    };
    static const ABoxEcPowerConfig power = {6000U, 2000U, 3000U, 0U, 1U};
    const ABoxBoardIoPort *board = ABoxBoardIo_HardwarePort();
    if (!configure_clock() || !board || !board->prepare || !board->prepare(board->context)) return 0;
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_bit_reset(GPIOC, (1U << 7) | (1U << 8) | (1U << 9));
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, (1U << 7) | (1U << 8) | (1U << 9));
    if (!ABoxGdMonotonic_Init() || !ABoxSerialPort_Init() || !ABoxGdModem_Init()) return 0;
    if (!ABox_PlatformPortBind(&port) || !ABox_EcPower_SetConfig(&power)) return 0;
    return 1;
}

int ABoxBoard_Init(uint32_t vector_address)
{
    SCB->VTOR = vector_address;
    __DSB();
    __ISB();
    if (!ABoxGdV1_Init()) return 0;
    __enable_irq();
    return 1;
}
