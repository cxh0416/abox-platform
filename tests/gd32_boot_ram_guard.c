#include <stdint.h>
/* Linked only into the RAM bench build. Fail closed before any flash change. */
volatile uint32_t g_abox_ram_guard_begin_calls;
int __wrap_ABoxGd32Flash_Begin(void *context)
{ (void)context; ++g_abox_ram_guard_begin_calls; return 0; }
int __wrap_ABoxGd32Flash_ErasePage(void *context, uint32_t address)
{ (void)context; (void)address; return 0; }
int __wrap_ABoxGd32Flash_Write(void *context, uint32_t address, const uint8_t *data, uint32_t length)
{ (void)context; (void)address; (void)data; (void)length; return 0; }
