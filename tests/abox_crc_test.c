#include "abox_crc.h"
#include "abox_crc_backend.h"
#include <assert.h>
#include <stddef.h>

static uint32_t crc;
static uint8_t scratch;
static unsigned clocks;
void ABoxCrcHw_Enable(void) { ++clocks; }
void ABoxCrcHw_Reset(void) { crc=0xffffffffU; }
void ABoxCrcHw_WriteWord(uint32_t word)
{
    crc^=word;
    for(unsigned i=0U;i<32U;++i)crc=(crc&0x80000000U) ? (crc<<1U)^0x04c11db7U : crc<<1U;
}
uint32_t ABoxCrcHw_Read(void) { return crc; }
void ABoxCrcHw_WriteScratch(uint8_t value) { scratch=value; }
uint8_t ABoxCrcHw_ReadScratch(void) { return scratch; }
int main(void)
{
    const uint32_t words[]={0x12345678U,0x9abcdef0U,0U};uint32_t value=0U;uint8_t aux=0U;
    assert(ABoxCrc_Read(&value)==ABOX_HAL_INVALID);
    assert(ABoxCrc_Open()==ABOX_HAL_OK && clocks==1U && ABoxCrc_Open()==ABOX_HAL_BUSY);
    assert(ABoxCrc_Read(&value)==ABOX_HAL_OK && value==0xffffffffU);
    assert(ABoxCrc_UpdateWords(NULL,1U)==ABOX_HAL_INVALID);
    assert(ABoxCrc_UpdateWords(NULL,0U)==ABOX_HAL_OK);
    assert(ABoxCrc_UpdateWords(words,1U)==ABOX_HAL_OK && ABoxCrc_Read(&value)==ABOX_HAL_OK && value==0xdf8a8a2bU);
    assert(ABoxCrc_UpdateWords(words+1,2U)==ABOX_HAL_OK && ABoxCrc_Read(&value)==ABOX_HAL_OK && value==0x1e75363fU);
    assert(ABoxCrc_WriteScratch(0xa5U)==ABOX_HAL_OK && ABoxCrc_Reset()==ABOX_HAL_OK);
    assert(ABoxCrc_ReadScratch(&aux)==ABOX_HAL_OK && aux==0xa5U);
    assert(ABoxCrc_Read(&value)==ABOX_HAL_OK && value==0xffffffffU);
    assert(ABoxCrc_Close()==ABOX_HAL_OK && ABoxCrc_Close()==ABOX_HAL_INVALID);
    return 0;
}
