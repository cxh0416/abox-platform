#include "abox_crc.h"
#include "abox_crc_backend.h"
#include <stddef.h>

static uint8_t opened;
ABoxHalStatus ABoxCrc_Open(void)
{
    if(opened)return ABOX_HAL_BUSY;
    ABoxCrcHw_Enable();ABoxCrcHw_Reset();opened=1U;return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCrc_Reset(void)
{ if(!opened)return ABOX_HAL_INVALID;ABoxCrcHw_Reset();return ABOX_HAL_OK; }
ABoxHalStatus ABoxCrc_UpdateWords(const uint32_t *words,uint32_t count)
{
    if(!opened || (count && (!words || ((uintptr_t)words&3U))))return ABOX_HAL_INVALID;
    for(uint32_t i=0U;i<count;++i)ABoxCrcHw_WriteWord(words[i]);
    return ABOX_HAL_OK;
}
ABoxHalStatus ABoxCrc_Read(uint32_t *result)
{ if(!opened || !result)return ABOX_HAL_INVALID;*result=ABoxCrcHw_Read();return ABOX_HAL_OK; }
ABoxHalStatus ABoxCrc_WriteScratch(uint8_t value)
{ if(!opened)return ABOX_HAL_INVALID;ABoxCrcHw_WriteScratch(value);return ABOX_HAL_OK; }
ABoxHalStatus ABoxCrc_ReadScratch(uint8_t *value)
{ if(!opened || !value)return ABOX_HAL_INVALID;*value=ABoxCrcHw_ReadScratch();return ABOX_HAL_OK; }
ABoxHalStatus ABoxCrc_Close(void)
{ if(!opened)return ABOX_HAL_INVALID;opened=0U;return ABOX_HAL_OK; }
