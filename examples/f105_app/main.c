#include "abox_board.h"
#include "boot_cfg.h"
#if defined(ABOX_CAN_PORT_ENABLED)
#include "abox_can_port.h"
#endif

void Error_Handler(void) { for (;;) {} }

int main(void)
{
    if (!ABoxBoard_Init(APP_START_ADDR)) Error_Handler();
#if defined(ABOX_CAN_PORT_ENABLED)
    /* Example uses silent loopback; real buses require an application contract. */
    if (!ABoxCanPort_Init(0U, 250000U, ABOX_CAN_SILENT_LOOPBACK)) Error_Handler();
#endif
    for (;;) {}
}
