#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gd32f10x.h"
#include "abox_boot_v2.h"
#include "abox_boot_v2_boot.h"
#include "abox_boot_v2_installer.h"
#include "abox_gd32f105_flash.h"
#include "abox_platform_port.h"
#include "ufs_at.h"
#include "boot_version.h"

#define APP_START 0x08008000U
#define APP_END   0x0803E800U
#define PAGE_SIZE 0x800U

uint32_t SystemCoreClock = 8000000U;
static volatile uint32_t ticks;
static uint8_t reset_reason;
static uint8_t image_buffer[1024];
#if defined(ABOX_GD_BOOT_RAM_SMOKE)
volatile uint32_t g_abox_ram_preflight_result;
#endif
#if defined(ABOX_GD_BOOT_FLASH_PROBE)
volatile uint32_t g_abox_flash_probe[9];
#endif
#if defined(ABOX_GD_BOOT_INSTALL_PROBE)
volatile uint32_t g_abox_install_probe[4];
#endif
#if defined(ABOX_GD_BOOT_UFS_INFO_PROBE)
volatile uint32_t g_abox_ufs_size_probe[4];
#endif

void SysTick_Handler(void) { ++ticks; }
static uint32_t tick(void *ctx) { (void)ctx; return ticks; }
static void delay(uint32_t ms) { uint32_t start = ticks; while ((uint32_t)(ticks - start) < ms) {} }
static void critical_enter(void *ctx) { (void)ctx; __disable_irq(); }
static void critical_leave(void *ctx) { (void)ctx; __enable_irq(); }

static int boot_clock_start(void)
{
    uint32_t count;
    if (rcu_system_clock_source_get() != RCU_SCSS_IRC8M || rcu_flag_get(RCU_FLAG_PLLSTB) != RESET) return 0;
    fmc_wscnt_set(WS_WSCNT_1);
    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV2);
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV1);
    rcu_pll_config(RCU_PLLSRC_IRC8M_DIV2, RCU_PLL_MUL9);
    rcu_osci_on(RCU_PLL_CK);
    if (rcu_osci_stab_wait(RCU_PLL_CK) != SUCCESS) return 0;
    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    for (count = 0U; count < 100000U; ++count)
        if (rcu_system_clock_source_get() == RCU_SCSS_PLL) break;
    if (count == 100000U || rcu_clock_freq_get(CK_AHB) != 36000000U) return 0;
    SystemCoreClock = 36000000U;
    return 1;
}

static int boot_clock_stop(void)
{
    uint32_t count;
    rcu_system_clock_source_config(RCU_CKSYSSRC_IRC8M);
    for (count = 0U; count < 100000U; ++count)
        if (rcu_system_clock_source_get() == RCU_SCSS_IRC8M) break;
    if (count == 100000U) return 0;
    rcu_osci_off(RCU_PLL_CK);
    for (count = 0U; count < 100000U; ++count)
        if (rcu_flag_get(RCU_FLAG_PLLSTB) == RESET) break;
    if (count == 100000U) return 0;
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV1);
    SystemCoreClock = 8000000U;
    return 1;
}

static int uart_write(void *ctx, const uint8_t *data, uint32_t length)
{
    uint32_t start = ticks;
    (void)ctx;
    for (uint32_t i = 0U; i < length; ++i) {
        while (usart_flag_get(USART0, USART_FLAG_TBE) == RESET)
            if ((uint32_t)(ticks - start) >= 5000U) return 0;
        usart_data_transmit(USART0, data[i]);
    }
    while (usart_flag_get(USART0, USART_FLAG_TC) == RESET)
        if ((uint32_t)(ticks - start) >= 5000U) return 0;
    return 1;
}

static void boot_log(void *ctx, const char *line)
{
    uint32_t start = ticks;
    (void)ctx;
    if (!line) return;
    while (*line) {
        while (usart_flag_get(UART4, USART_FLAG_TBE) == RESET)
            if ((uint32_t)(ticks - start) >= 200U) return;
        usart_data_transmit(UART4, (uint8_t)*line++);
    }
    while (usart_flag_get(UART4, USART_FLAG_TC) == RESET)
        if ((uint32_t)(ticks - start) >= 200U) return;
}

uint32_t ABoxGdBootTick(void) { return ticks; }
int ABoxGdBootUartWrite(void *ctx, const uint8_t *data, uint32_t length)
{ return uart_write(ctx, data, length); }
int ABoxGdBootUartRead(uint8_t *value, uint32_t timeout)
{
    uint32_t start = ticks;
    do {
        uint32_t status = USART_STAT(USART0);
        if (status & (USART_STAT_PERR | USART_STAT_FERR | USART_STAT_NERR | USART_STAT_ORERR)) {
            (void)usart_data_receive(USART0); return -1;
        }
        if (status & USART_STAT_RBNE) { *value = (uint8_t)usart_data_receive(USART0); return 1; }
    } while ((uint32_t)(ticks - start) < timeout);
    return 0;
}

static void uart_drain_startup(void)
{
    uint32_t start = ticks, last_byte = ticks;
    while ((uint32_t)(ticks - start) < 200U && (uint32_t)(ticks - last_byte) < 20U) {
        uint32_t status = USART_STAT(USART0);
        if (status & (USART_STAT_RBNE | USART_STAT_PERR | USART_STAT_FERR | USART_STAT_NERR | USART_STAT_ORERR)) {
            (void)usart_data_receive(USART0);
            last_byte = ticks;
        }
    }
}

static int flash_read(void *ctx, uint32_t address, uint8_t *data, uint32_t length)
{ (void)ctx; memcpy(data, (const void *)(uintptr_t)address, length); return 1; }
static int flash_write_word(void *ctx, uint32_t address, const uint8_t word[4])
{ return ABoxGd32Flash_Write(ctx, address, word, 4U); }
static int vector_valid(void *ctx, const uint8_t vector[8])
{ (void)ctx; return ABoxBootV2_ImageVectorValid(vector); }
static const ABoxBootV2Installer installer = {
    0, ABoxGdBootUfs_Open, ABoxGdBootUfs_Read, ABoxGdBootUfs_Close, ABoxGd32Flash_Begin, ABoxGd32Flash_ErasePage,
    flash_write_word, flash_read, ABoxGd32Flash_End, vector_valid,
    APP_START, APP_END, PAGE_SIZE, image_buffer, sizeof(image_buffer)
};
static uint8_t flash_valid(void *ctx, uint32_t size, uint32_t crc)
{
    (void)ctx;
    return size >= 8U && size <= APP_END - APP_START &&
        vector_valid(0, (const uint8_t *)APP_START) &&
        ABoxBootV2_Crc32((const void *)APP_START, size) == crc;
}
static uint8_t app_valid(void *ctx) { return (uint8_t)vector_valid(ctx, (const uint8_t *)APP_START); }
static ABoxBootV2InstallResult install(void *ctx, uint8_t slot, uint32_t size, uint32_t crc)
{ (void)ctx; return ABoxBootV2_Install(&installer, slot, size, crc); }
static uint8_t reset_cause(void *ctx) { (void)ctx; return reset_reason; }
static void boot_delay(void *ctx, uint32_t ms) { (void)ctx; delay(ms); }
static void jump(void *ctx)
{
    uint32_t sp = *(const uint32_t *)APP_START, pc = *(const uint32_t *)(APP_START + 4U);
    (void)ctx;
    __disable_irq(); SysTick->CTRL = 0U; SysTick->LOAD = 0U; SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    for (unsigned i = 0; i < 8U; ++i) { NVIC->ICER[i] = 0xFFFFFFFFU; NVIC->ICPR[i] = 0xFFFFFFFFU; }
    usart_deinit(USART0); usart_deinit(UART4);
    if (!boot_clock_stop()) for (;;) {}
    SCB->VTOR = APP_START; __DSB(); __ISB();
    __asm volatile("msr msp, %0\n bx %1" :: "r"(sp), "r"(pc) : "memory");
    __builtin_unreachable();
}

int main(void)
{
    static const ABoxFlashLayout flash_layout = { APP_START, 0x0803F800U, PAGE_SIZE };
    static const ABoxBootV2Layout layout = { 0x0803E800U, 0x0803F000U, PAGE_SIZE,
        APP_START, APP_END - 1U, 0x20000000U, 0x20010000U };
    static const ABoxPlatformPort port = { .get_tick_ms = tick, .uart_write = uart_write,
        .flash_begin = ABoxGd32Flash_Begin, .flash_write = ABoxGd32Flash_Write,
        .flash_erase_page = ABoxGd32Flash_ErasePage, .flash_end = ABoxGd32Flash_End,
        .enter_critical = critical_enter, .exit_critical = critical_leave, .flash_layout = &flash_layout };
    static const ABoxBootV2BootPort boot = { 0, flash_valid, install, app_valid, jump, reset_cause, boot_delay, boot_log };
    reset_reason = rcu_flag_get(RCU_FLAG_FWDGTRST) ? 3U : 0U;
    rcu_all_reset_flag_clear();
    if (!boot_clock_start() || SysTick_Config(36000U)) for (;;) {}
    __enable_irq();
    rcu_periph_clock_enable(RCU_GPIOA); rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_USART0); rcu_periph_clock_enable(RCU_UART4);
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, 1U << 9);
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, 1U << 10);
    gpio_init(GPIOC, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, 1U << 12);
    gpio_bit_reset(GPIOC, (1U << 7) | (1U << 8) | (1U << 9));
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, (1U << 7) | (1U << 8) | (1U << 9));
    usart_deinit(USART0); usart_baudrate_set(USART0, 115200U); usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT); usart_parity_config(USART0, USART_PM_NONE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE); usart_receive_config(USART0, USART_RECEIVE_ENABLE); usart_enable(USART0);
    usart_deinit(UART4); usart_baudrate_set(UART4, 115200U); usart_word_length_set(UART4, USART_WL_8BIT);
    usart_stop_bit_set(UART4, USART_STB_1BIT); usart_parity_config(UART4, USART_PM_NONE);
    usart_transmit_config(UART4, USART_TRANSMIT_ENABLE); usart_enable(UART4);
    boot_log(0, "[BOOT2] evt=BOOT_START version=" ABOX_GD_BOOT_VERSION "\r\n");
    if (!ABox_PlatformPortBind(&port) || !ABoxBootV2_StateBindLayout(&layout) || !ABoxBootV2Boot_Init(&boot)) for (;;) {}
    delay(1000U); gpio_bit_set(GPIOC, 1U << 7); delay(1000U); gpio_bit_set(GPIOC, 1U << 8);
    delay(2000U); gpio_bit_reset(GPIOC, 1U << 8); delay(2500U);
    uart_drain_startup();
    int ufs_ready = ABoxGdBootUfs_Init();
#if defined(ABOX_GD_BOOT_RAM_SMOKE)
#if defined(ABOX_GD_BOOT_UFS_INFO_PROBE)
    if (ufs_ready) {
        uint32_t size = 0U;
        g_abox_ufs_size_probe[0] = (uint32_t)ABoxGdBootUfs_GetSize(0U, &size);
        g_abox_ufs_size_probe[1] = size;
        size = 0U;
        g_abox_ufs_size_probe[2] = (uint32_t)ABoxGdBootUfs_GetSize(1U, &size);
        g_abox_ufs_size_probe[3] = size;
    }
#else
    if (ufs_ready) {
        ABoxBootV2Record record;
        if (ABoxBootV2_StateLoad(&record) && record.state == ABOX_BOOT_V2_CONFIRMED &&
            record.stable_slot <= 1U && record.stable_image_size >= 8U) {
            g_abox_ram_preflight_result = 1U;
            if (ABoxBootV2_Install(&installer, record.stable_slot, record.stable_image_size,
                                  record.stable_image_crc32) == ABOX_BOOT_V2_INSTALL_PRE_ERASE_FAILED)
                g_abox_ram_preflight_result = 2U;
        }
    }
#endif
    /* Give the bench observer time to sample the UFS initialization result. */
    delay(2000U);
#elif defined(ABOX_GD_BOOT_FLASH_PROBE)
    {
        ABoxBootV2Record record;
        if (ABoxBootV2_StateLoad(&record) && record.state == ABOX_BOOT_V2_CONFIRMED) {
            const uint32_t test_page = 0x08007000U;
            const uint8_t pattern_a[4] = {0x35U, 0xCAU, 0x5AU, 0xA5U};
            const uint8_t pattern_b[4] = {0x96U, 0x69U, 0xC3U, 0x3CU};
            int fill_valid = 1;
            for (uint32_t i = 0; i < PAGE_SIZE; ++i)
                if (*(const volatile uint8_t *)(test_page + i) != 0U) { fill_valid = 0; break; }
            g_abox_flash_probe[0] = FMC_CTL0;
            g_abox_flash_probe[1] = (uint32_t)fill_valid;
            if (fill_valid) g_abox_flash_probe[2] = (uint32_t)ABoxGd32Flash_Begin(0);
            if (g_abox_flash_probe[2]) {
                g_abox_flash_probe[3] = (uint32_t)ABoxGd32Flash_ErasePage(0, test_page);
                if (g_abox_flash_probe[3]) {
                    g_abox_flash_probe[4] = (uint32_t)(
                        ABoxGd32Flash_Write(0, test_page, pattern_a, sizeof(pattern_a)) &&
                        ABoxGd32Flash_Write(0, test_page + PAGE_SIZE - 4U, pattern_b, sizeof(pattern_b)));
                    g_abox_flash_probe[5] = (uint32_t)(
                        !memcmp((const void *)test_page, pattern_a, sizeof(pattern_a)) &&
                        !memcmp((const void *)(test_page + PAGE_SIZE - 4U), pattern_b, sizeof(pattern_b)));
                    g_abox_flash_probe[6] = (uint32_t)ABoxGd32Flash_ErasePage(0, test_page);
                    if (g_abox_flash_probe[6]) {
                        g_abox_flash_probe[7] = 1U;
                        for (uint32_t i = 0; i < PAGE_SIZE; ++i)
                            if (*(const volatile uint8_t *)(test_page + i) != 0xFFU) { g_abox_flash_probe[7] = 0U; break; }
                    }
                }
            }
            ABoxGd32Flash_End(0);
            g_abox_flash_probe[8] = FMC_CTL0;
            delay(2000U);
        }
    }
    (void)ufs_ready;
#elif defined(ABOX_GD_BOOT_INSTALL_PROBE)
    if (ufs_ready) {
        ABoxBootV2Record record;
        if (ABoxBootV2_StateLoad(&record) && record.state == ABOX_BOOT_V2_CONFIRMED &&
            record.stable_slot <= 1U && record.stable_image_size >= 8U) {
            g_abox_install_probe[0] = record.stable_image_size;
            g_abox_install_probe[1] = record.stable_image_crc32;
            g_abox_install_probe[2] = 1U + (uint32_t)ABoxBootV2_Install(
                &installer, record.stable_slot, record.stable_image_size, record.stable_image_crc32);
            g_abox_install_probe[3] = flash_valid(0, record.stable_image_size, record.stable_image_crc32);
            delay(3000U);
        }
    }
#elif defined(ABOX_GD_BOOT_UFS_INFO_PROBE)
    if (ufs_ready) {
        uint32_t size = 0U;
        g_abox_ufs_size_probe[0] = (uint32_t)ABoxGdBootUfs_GetSize(0U, &size);
        g_abox_ufs_size_probe[1] = size;
        size = 0U;
        g_abox_ufs_size_probe[2] = (uint32_t)ABoxGdBootUfs_GetSize(1U, &size);
        g_abox_ufs_size_probe[3] = size;
        delay(2000U);
    }
#else
    (void)ufs_ready;
#endif
    for (;;) ABoxBootV2Boot_Task();
}
