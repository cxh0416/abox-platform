#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_wwdg.h"
#include "abox_wwdg_backend.h"

static ABoxWwdgRegisters fake;
static unsigned clock_enabled,freeze,callbacks,critical;
uint32_t ABoxCritical_Enter(void) { assert(!critical);critical=1U;return 0U; }
void ABoxCritical_Exit(uint32_t previous) { assert(critical && !previous);critical=0U; }
ABoxWwdgRegisters *ABoxWwdgHw_Enable(void) { assert(critical);++clock_enabled;return &fake; }
void ABoxWwdgHw_Start(ABoxWwdgRegisters *r,uint8_t counter,uint32_t config)
{ assert(critical && r==&fake);r->configuration=config;r->control=0x80U|counter; }
void ABoxWwdgHw_SetDebugFreeze(uint8_t enabled) { assert(critical);freeze=enabled; }
static void early(void *context)
{
    assert(context==(void *)1 && fake.status==0U);
    ++callbacks;assert(ABoxWwdg_Refresh(127U)==ABOX_HAL_OK);
}
int main(int argc,char **argv)
{
    ABoxWwdgConfig c={8U,80U,127U,1U};ABoxWwdgEvents e={(void *)1,early};uint8_t current=0U;
    if (argc>1 && strcmp(argv[1],"attach")==0) {
        assert(ABoxWwdg_Attach(&e)==ABOX_HAL_INVALID);
        fake.control=0x80U|64U;fake.configuration=0x200U|0x180U|80U;fake.status=1U;
        assert(ABoxWwdg_Start(&c,&e)==ABOX_HAL_BUSY);
        assert(ABoxWwdg_Attach(&e)==ABOX_HAL_OK);
        assert(fake.control==(0x80U|64U) && fake.configuration==(0x200U|0x180U|80U) && fake.status==1U);
        assert(ABoxWwdg_Attach(&e)==ABOX_HAL_BUSY);
        ABoxWwdg_Irq();assert(callbacks==1U && fake.control==0xffU && !fake.status);
        return 0;
    }
    assert(ABoxWwdg_ReadCounter(&current)==ABOX_HAL_INVALID);
    assert(ABoxWwdg_Refresh(127U)==ABOX_HAL_INVALID);
    c.prescaler=3U;assert(ABoxWwdg_Start(&c,&e)==ABOX_HAL_INVALID && !clock_enabled);c.prescaler=8U;
    c.initial_counter=63U;assert(ABoxWwdg_Start(&c,&e)==ABOX_HAL_INVALID);c.initial_counter=127U;
    assert(ABoxWwdg_SetDebugFreeze(2U)==ABOX_HAL_INVALID);
    assert(ABoxWwdg_SetDebugFreeze(1U)==ABOX_HAL_OK && freeze);
    assert(ABoxWwdg_SetDebugFreeze(0U)==ABOX_HAL_OK && !freeze);
    fake.control=0xffU;assert(ABoxWwdg_Start(&c,&e)==ABOX_HAL_BUSY);
    fake.control=0x7fU;fake.status=1U;
    if (argc>1) c.early_interrupt=0U;
    assert(ABoxWwdg_Start(&c,&e)==ABOX_HAL_OK && !fake.status);
    assert(fake.control==0xffU && fake.configuration==(0x180U|80U|(argc>1?0U:0x200U)));
    assert(ABoxWwdg_Start(&c,&e)==ABOX_HAL_BUSY);
    assert(ABoxWwdg_ReadCounter(&current)==ABOX_HAL_OK && current==127U);
    assert(ABoxWwdg_ReadCounter(0)==ABOX_HAL_INVALID);
    assert(ABoxWwdg_Refresh(63U)==ABOX_HAL_INVALID && fake.control==0xffU);
    fake.control=0x80U|80U;assert(ABoxWwdg_Refresh(127U)==ABOX_HAL_BUSY && fake.control==(0x80U|80U));
    fake.control=0x80U|79U;assert(ABoxWwdg_Refresh(127U)==ABOX_HAL_OK && fake.control==0xffU);
    fake.control=0x80U|63U;assert(ABoxWwdg_Refresh(127U)==ABOX_HAL_TIMEOUT);
    ABoxWwdg_Irq();assert(!callbacks);
    fake.control=0x80U|64U;fake.status=1U;ABoxWwdg_Irq();
    if (argc>1) assert(!callbacks && fake.status==1U && fake.control==(0x80U|64U));
    else assert(callbacks==1U && fake.control==0xffU && !fake.status);
    return 0;
}
