#ifndef ABOX_CRC_BACKEND_H
#define ABOX_CRC_BACKEND_H
#include <stdint.h>
void ABoxCrcHw_Enable(void);
void ABoxCrcHw_Reset(void);
void ABoxCrcHw_WriteWord(uint32_t word);
uint32_t ABoxCrcHw_Read(void);
void ABoxCrcHw_WriteScratch(uint8_t value);
uint8_t ABoxCrcHw_ReadScratch(void);
#endif
