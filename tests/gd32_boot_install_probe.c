#include <stdint.h>
volatile uint32_t g_abox_install_erases, g_abox_install_writes;
int __real_ABoxGd32Flash_ErasePage(void *context, uint32_t address);
int __real_ABoxGd32Flash_Write(void *context, uint32_t address, const uint8_t *data, uint32_t length);
int __wrap_ABoxGd32Flash_ErasePage(void *context, uint32_t address)
{ ++g_abox_install_erases; return __real_ABoxGd32Flash_ErasePage(context, address); }
int __wrap_ABoxGd32Flash_Write(void *context, uint32_t address, const uint8_t *data, uint32_t length)
{ ++g_abox_install_writes; return __real_ABoxGd32Flash_Write(context, address, data, length); }
