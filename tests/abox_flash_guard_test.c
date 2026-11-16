#include <assert.h>
#include <stdint.h>
#include "abox_flash_backend.h"
static unsigned real_calls, begin_calls, write_calls, erase_calls;
int ABoxGd32Flash_Begin(void *c) { (void)c; ++real_calls; return 1; }
int ABoxGd32Flash_Write(void *c,uint32_t a,const uint8_t *d,uint32_t n)
{ (void)c;(void)a;(void)d;(void)n;++real_calls;return 1; }
int ABoxGd32Flash_ErasePage(void *c,uint32_t a) { (void)c;(void)a;++real_calls;return 1; }
void ABoxGd32Flash_End(void *c) { (void)c; }
int __wrap_ABoxGd32Flash_Begin(void *c) { (void)c;++begin_calls;return 0; }
int __wrap_ABoxGd32Flash_Write(void *c,uint32_t a,const uint8_t *d,uint32_t n)
{ (void)c;(void)a;(void)d;(void)n;++write_calls;return 0; }
int __wrap_ABoxGd32Flash_ErasePage(void *c,uint32_t a) { (void)c;(void)a;++erase_calls;return 0; }
int main(void)
{
    uint8_t d[4]={0};
    assert(ABoxFlash_Begin()==ABOX_HAL_ERROR && begin_calls==1U);
    assert(ABoxFlashHw_Program(0x0803F800U,d,4U)==0 && write_calls==1U);
    assert(ABoxFlashHw_ErasePage(0x0803F800U)==0 && erase_calls==1U);
    assert(!real_calls);return 0;
}
