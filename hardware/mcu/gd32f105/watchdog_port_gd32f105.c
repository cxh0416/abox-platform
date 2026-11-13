#include "abox_watchdog_port.h"
#include "gd32f10x.h"

/* GD32F10x FWDGT registers and keys. The watchdog cannot be stopped after
 * enable; a failed configuration therefore deliberately expires. */
#define WD_CTL REG32(FWDGT_BASE + 0x00U)
#define WD_PSC REG32(FWDGT_BASE + 0x04U)
#define WD_RLD REG32(FWDGT_BASE + 0x08U)
#define WD_STAT REG32(FWDGT_BASE + 0x0CU)
#define WD_KEY_START 0xCCCCU
#define WD_KEY_WRITE 0x5555U
#define WD_KEY_RELOAD 0xAAAAU
#define WD_TIMEOUT 0x000FFFFFU

static uint8_t ready;

static int wait_clear(uint32_t mask)
{
    uint32_t remaining = WD_TIMEOUT;
    while ((WD_STAT & mask) && --remaining) {}
    return (WD_STAT & mask) == 0U;
}

int ABoxWatchdog_Init(uint16_t prescaler, uint16_t reload)
{
    uint8_t encoded = 0U;
    uint16_t value = 4U;
    if (ready || reload > 4095U) return 0;
    while (value < prescaler && encoded < 6U) { value <<= 1U; ++encoded; }
    if (value != prescaler) return 0;
    WD_CTL = WD_KEY_START;
    WD_CTL = WD_KEY_WRITE;
    WD_PSC = encoded;
    if (!wait_clear(1U)) return 0;
    WD_RLD = reload;
    if (!wait_clear(2U)) return 0;
    WD_CTL = WD_KEY_RELOAD;
    ready = 1U;
    return 1;
}

int ABoxWatchdog_Refresh(void)
{
    if (!ready) return 0;
    WD_CTL = WD_KEY_RELOAD;
    return 1;
}

void ABoxWatchdog_FreezeWhileDebugging(void)
{ dbg_periph_enable(DBG_FWDGT_HOLD); }
