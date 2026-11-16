#include "abox_crc_backend.h"
#include "gd32f10x.h"
void ABoxCrcHw_Enable(void) { rcu_periph_clock_enable(RCU_CRC); }
void ABoxCrcHw_Reset(void) { REG32(CRC_BASE+8U)=1U; }
void ABoxCrcHw_WriteWord(uint32_t word) { REG32(CRC_BASE)=word; }
uint32_t ABoxCrcHw_Read(void) { return REG32(CRC_BASE); }
void ABoxCrcHw_WriteScratch(uint8_t value) { REG32(CRC_BASE+4U)=value; }
uint8_t ABoxCrcHw_ReadScratch(void) { return (uint8_t)REG32(CRC_BASE+4U); }
