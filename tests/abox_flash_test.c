#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abox_flash_backend.h"

static const ABoxFlashGeometry geometry={0x1000U,256U,128U,2U,4U};
static uint8_t bytes[256];
static unsigned begin_count, end_count, programs, erases, fail_program, fail_end;
static uint32_t lengths[8],addresses[8];
const ABoxFlashGeometry *ABoxFlashHw_Geometry(void) { return &geometry; }
int ABoxFlashHw_Begin(void) { ++begin_count;return 1; }
int ABoxFlashHw_Program(uint32_t address,const uint8_t *data,uint32_t length)
{
    assert(programs<8U);lengths[programs]=length;addresses[programs]=address;++programs;
    if(fail_program==programs)return 0;
    memcpy(bytes+address-geometry.base,data,length);return 1;
}
int ABoxFlashHw_ErasePage(uint32_t address) { ++erases;memset(bytes+address-geometry.base,0xff,geometry.erase_size);return 1; }
int ABoxFlashHw_End(void) { ++end_count;return !fail_end; }
int ABoxFlashHw_Read(uint32_t address,uint8_t *data,uint32_t length) { memcpy(data,bytes+address-geometry.base,length);return 1; }
int main(void)
{
    ABoxFlashGeometry found;
    uint8_t source[]={1,2,3,4,5,6},out[6]={0};
    assert(ABoxFlash_Geometry(0)==ABOX_HAL_INVALID);
    assert(ABoxFlash_Geometry(&found)==ABOX_HAL_OK && found.base==0x1000U && found.erase_size==128U);
    assert(ABoxFlash_Program(0x1000U,source,2U)==ABOX_HAL_INVALID);
    assert(ABoxFlash_Begin()==ABOX_HAL_OK && ABoxFlash_Begin()==ABOX_HAL_BUSY && begin_count==1U);
    assert(ABoxFlash_Program(0x1001U,source,2U)==ABOX_HAL_INVALID);
    assert(ABoxFlash_Program(0x1000U,source,3U)==ABOX_HAL_INVALID);
    assert(ABoxFlash_Program(0x10feU,source,4U)==ABOX_HAL_INVALID && !programs);
    assert(ABoxFlash_Program(0x1002U,source,6U)==ABOX_HAL_OK);
    assert(programs==2U && lengths[0]==2U && lengths[1]==4U && addresses[1]==0x1004U);
    assert(ABoxFlash_Read(0x1002U,out,6U)==ABOX_HAL_OK && memcmp(out,source,6U)==0);
    assert(ABoxFlash_Read(0xfffffffeU,out,6U)==ABOX_HAL_INVALID);
    assert(ABoxFlash_ErasePage(0x1081U)==ABOX_HAL_INVALID && !erases);
    assert(ABoxFlash_ErasePage(0x1080U)==ABOX_HAL_OK && erases==1U);
    fail_program=4U;assert(ABoxFlash_Program(0x1002U,source,6U)==ABOX_HAL_ERROR && programs==4U);
    fail_end=1U;assert(ABoxFlash_End()==ABOX_HAL_ERROR && ABoxFlash_Begin()==ABOX_HAL_BUSY);
    fail_end=0U;assert(ABoxFlash_End()==ABOX_HAL_OK && ABoxFlash_End()==ABOX_HAL_INVALID && end_count==2U);
    return 0;
}
