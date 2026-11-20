#include "abox_board.h"
#include "boot_cfg.h"
#if defined(ABOX_CAN_PORT_ENABLED)
#include "abox_can_port.h"
#endif

#if defined(ABOX_EXAMPLE_NOR)
#include "abox_board_v3_nor.h"
#include "abox_platform_port.h"
static ABoxNor nor;
volatile uint32_t g_abox_example_nor_id;
#endif
#if defined(ABOX_EXAMPLE_NOR_IDEM)
#include "abox_mqtt_idem_nor.h"
static ABoxMqttIdemNor idem;
volatile uint8_t g_abox_example_idem_ready;
#endif
#if defined(ABOX_EXAMPLE_NOR_OTA)
#include "abox_boot_v2.h"
volatile uint8_t g_abox_example_nor_boot_matches;
#endif

#if defined(ABOX_EXAMPLE_NOR_PROBE)
#include "abox_boot_v2_nor.h"
#include <string.h>

#define PROBE_SECTOR (ABOX_NOR_OTA_SLOT_SIZE - ABOX_NOR_SECTOR_SIZE)
_Static_assert(APP_MAX_SIZE + ABOX_BOOT_V2_NOR_HEADER_SIZE <= PROBE_SECTOR,
               "This App layout must reserve the last sector of OTA slot 1");

volatile uint8_t g_abox_example_nor_probe;

static int erase_probe_sector(void)
{
    if (ABoxNor_StartErase(&nor, ABOX_NOR_OTA_SLOT1, PROBE_SECTOR) != ABOX_NOR_OK) return 0;
    ABoxNorResult result;
    do { result = ABoxNor_Status(&nor); } while (result == ABOX_NOR_BUSY);
    return result == ABOX_NOR_OK;
}

static int test_nor_sector(void)
{
    const uint32_t offset = PROBE_SECTOR + ABOX_NOR_PAGE_SIZE - 2U;
    const uint8_t pattern[] = {0x35U, 0xCAU, 0x5AU, 0xA5U, 0xC3U, 0x3CU};
    uint8_t erased[sizeof(pattern)], readback[sizeof(pattern)];
    memset(erased, 0xFF, sizeof(erased));
    return erase_probe_sector() &&
        ABoxNor_Read(&nor, ABOX_NOR_OTA_SLOT1, offset, readback, sizeof(readback)) == ABOX_NOR_OK &&
        memcmp(readback, erased, sizeof(readback)) == 0 &&
        ABoxNor_Program(&nor, ABOX_NOR_OTA_SLOT1, offset, pattern, sizeof(pattern)) == ABOX_NOR_OK &&
        ABoxNor_Read(&nor, ABOX_NOR_OTA_SLOT1, offset, readback, sizeof(readback)) == ABOX_NOR_OK &&
        memcmp(readback, pattern, sizeof(readback)) == 0 &&
        erase_probe_sector() &&
        ABoxNor_Read(&nor, ABOX_NOR_OTA_SLOT1, offset, readback, sizeof(readback)) == ABOX_NOR_OK &&
        memcmp(readback, erased, sizeof(readback)) == 0;
}
#endif

void Error_Handler(void) { for (;;) {} }

int main(void)
{
    const ABoxBoardUartConfig uarts[] = {
        {ABOX_BOARD_UART_MODEM, {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}},
#ifdef ABOX_BOARD_V3
        {ABOX_BOARD_UART_RS232, {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}},
        {ABOX_BOARD_UART_TTL, {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}},
#endif
        {ABOX_BOARD_UART_BUS, {115200U, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}}
    };
    const ABoxMcuClockConfig clock = {ABOX_MCU_CLOCK_PLL_HSE, 8000000U, 1U, 9U, 1U, 2U, 1U, 0U, 0U};
    const ABoxBoardConfig board = {APP_START_ADDR, uarts, sizeof(uarts) / sizeof(uarts[0]), &clock};
    if (!ABoxBoard_Init(&board)) Error_Handler();
#if defined(ABOX_CAN_PORT_ENABLED)
    /* Example uses silent loopback; real buses require an application contract. */
    const ABoxCanConfig can = {250000U, 15U, 2U, 1U, ABOX_CAN_SILENT_LOOPBACK, {1U, 0U, 1U}, {0}};
    if (!ABoxCanPort_Init(0U, &can)) Error_Handler();
#endif
#if defined(ABOX_EXAMPLE_NOR)
    uint32_t id = 0U;
    int nor_ready = ABoxBoardV3_NorOpen(&nor, ABox_PlatformPortGet()->get_tick_ms, &id);
    g_abox_example_nor_id = id;
    if (!nor_ready) Error_Handler();
#endif
#if defined(ABOX_EXAMPLE_NOR_PROBE)
    g_abox_example_nor_probe = test_nor_sector() ? 1U : 2U;
#endif
#if defined(ABOX_EXAMPLE_NOR_OTA)
    ABoxBootV2Descriptor descriptor;
    g_abox_example_nor_boot_matches = ABoxBootV2_DescriptorRead(&descriptor) &&
        ABoxBootV2_DescriptorReadyFor(&descriptor, ABOX_BOOT_V2_FEATURE_NOR_AB);
#endif
#if defined(ABOX_EXAMPLE_NOR_IDEM)
    if (!ABoxMqttIdemNor_Init(&idem, &nor, "reference-v3")) Error_Handler();
#endif
    for (;;) {
#if defined(ABOX_EXAMPLE_NOR_IDEM)
        ABoxMqttIdem_Poll(&idem.cache);
        g_abox_example_idem_ready = idem.cache.ready && !idem.cache.failed;
#endif
    }
}
