#include "abox_gd32_v1.h"
#include "abox_board.h"
#include "abox_board_io.h"
#include "abox_flash.h"
#include "abox_gd32_monotonic.h"
#include "abox_platform_port.h"
#include "boot_cfg.h"
#include "ec_power.h"
#include "gd32f10x.h"
#ifdef ABOX_BOARD_V3
#include "abox_board_v3.h"
static uint8_t modem_powered, board_ready;
#endif
#ifdef ABOX_V1_CAN_BOARD
int ABoxGdV1Can_Bind(void);
#endif

uint32_t SystemCoreClock = 8000000U;

static uint32_t tick(void *context) { (void)context; return (uint32_t)ABoxGdMonotonic_GetMs(); }
static uint64_t monotonic(void *context) { (void)context; return ABoxGdMonotonic_GetMs(); }
static int uart_write(void *context, const uint8_t *data, uint32_t length)
{ (void)context; return length && length <= 65535U && ABoxUart_SendBlocking(ABOX_BOARD_UART_MODEM, data, (uint16_t)length, 1000U); }
static int flash_begin(void *context) { (void)context; return ABoxFlash_Begin() == ABOX_HAL_OK; }
static int flash_write(void *context, uint32_t address, const uint8_t *data, uint32_t length)
{ (void)context; return ABoxFlash_Program(address, data, length) == ABOX_HAL_OK; }
static int flash_erase(void *context, uint32_t address)
{ (void)context; return ABoxFlash_ErasePage(address) == ABOX_HAL_OK; }
static void flash_end(void *context) { (void)context; (void)ABoxFlash_End(); }
static int flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length)
{ (void)context; return ABoxFlash_Read(address, data, length) == ABOX_HAL_OK; }
static void critical_enter(void *context) { (void)context; __disable_irq(); }
static void critical_leave(void *context) { (void)context; __enable_irq(); }
static void power_write(void *context, uint8_t level)
{
    (void)context;
#ifdef ABOX_BOARD_V3
    modem_powered = !!level;
#endif
    if (level) gpio_bit_set(GPIOC, 1U << 7);
    else gpio_bit_reset(GPIOC, 1U << 7);
}
#ifdef ABOX_BOARD_V3
int ABoxBoardV3_SelectSim(ABoxBoardV3Sim sim)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    int selected = board_ready && !modem_powered &&
        (sim == ABOX_BOARD_V3_SIM_PHYSICAL || sim == ABOX_BOARD_V3_SIM_ESIM);
    if (selected) gpio_bit_write(GPIOC, GPIO_PIN_5, sim == ABOX_BOARD_V3_SIM_ESIM ? SET : RESET);
    __set_PRIMASK(key);
    return selected;
}
#endif
static void pwrkey_write(void *context, uint8_t level)
{ (void)context; if (level) gpio_bit_set(GPIOC, 1U << 8); else gpio_bit_reset(GPIOC, 1U << 8); }

static int gd_board_init(const ABoxBoardConfig *config)
{
    static const ABoxFlashLayout layout = {APP_START_ADDR, OTA_INFO_ADDR, OTA_FLASH_PAGE_SIZE};
    static const ABoxPlatformPort port = {
        .get_tick_ms = tick, .uart_write = uart_write,
        .flash_begin = flash_begin, .flash_write = flash_write,
        .flash_erase_page = flash_erase, .flash_end = flash_end, .flash_read = flash_read,
        .enter_critical = critical_enter, .exit_critical = critical_leave,
        .flash_layout = &layout, .ec_power_enable_write = power_write,
        .ec_pwrkey_write = pwrkey_write, .get_monotonic_ms = monotonic
    };
    static const ABoxEcPowerConfig power = {6000U, 2000U, 3000U, 0U, 1U};
    const ABoxBoardIoPort *board = ABoxBoardIo_HardwarePort();
    if (!config || !config->clock || ABoxMcuClock_Apply(config->clock) != ABOX_HAL_OK ||
        !board || !board->prepare || !board->prepare(board->context)) return 0;
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_bit_reset(GPIOC, (1U << 7) | (1U << 8) | (1U << 9));
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, (1U << 7) | (1U << 8) | (1U << 9));
#ifdef ABOX_BOARD_V3
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, GPIO_PIN_8);
    gpio_bit_reset(GPIOC, GPIO_PIN_5);
    modem_powered = 0U;
#endif
    if (!ABoxGdMonotonic_Init()) return 0;
    for (unsigned i = 0U; i < config->uart_count; ++i)
        if (!ABoxBoardUart_Prepare(config->uarts[i].id) ||
            !ABoxUart_Open(config->uarts[i].id, &config->uarts[i].config)) return 0;
    if (!ABox_PlatformPortBind(&port) || !ABox_EcPower_SetConfig(&power)) return 0;
    return 1;
}

#ifndef ABOX_BOARD_V3
int ABoxGdV1_Init(const ABoxBoardConfig *config)
{
    return gd_board_init(config);
}
#endif

int ABoxBoard_Init(const ABoxBoardConfig *config)
{
    if (!config || !config->clock || (config->uart_count && !config->uarts)) return 0;
#ifdef ABOX_V1_CAN_BOARD
    if (!ABoxGdV1Can_Bind()) return 0;
#endif
    SCB->VTOR = config->vector_address;
    __DSB();
    __ISB();
    if (!gd_board_init(config)) return 0;
#ifdef ABOX_BOARD_V3
    board_ready = 1U;
#endif
    __enable_irq();
    return 1;
}
