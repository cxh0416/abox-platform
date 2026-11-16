#include "abox_crc_backend.h"
#include <assert.h>
#include <stdint.h>
unsigned crc_clocks;
#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
CRC_TypeDef test_crc;
#define DATA test_crc.DR
#define CONTROL test_crc.CR
#else
#include "gd32f10x.h"
uint32_t test_crc_words[3];
void rcu_periph_clock_enable(uint32_t id) { assert(id==RCU_CRC);++crc_clocks; }
#define DATA test_crc_words[0]
#define CONTROL test_crc_words[2]
#endif
int main(void)
{
    ABoxCrcHw_Enable();assert(crc_clocks==1U);
    ABoxCrcHw_Reset();assert(CONTROL==1U);
    ABoxCrcHw_WriteWord(0x12345678U);assert(DATA==0x12345678U);
    DATA=0xdeadbeefU;assert(ABoxCrcHw_Read()==0xdeadbeefU);
    ABoxCrcHw_WriteScratch(0x5aU);assert(ABoxCrcHw_ReadScratch()==0x5aU);
    assert(DATA==0xdeadbeefU && CONTROL==1U);
    return 0;
}
