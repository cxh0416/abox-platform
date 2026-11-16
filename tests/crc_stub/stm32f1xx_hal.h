#ifndef ABOX_TEST_STM32_CRC_H
#define ABOX_TEST_STM32_CRC_H
#include <stdint.h>
typedef struct { volatile uint32_t DR;volatile uint8_t IDR;uint8_t reserved[3];volatile uint32_t CR; } CRC_TypeDef;
extern CRC_TypeDef test_crc;
extern unsigned crc_clocks;
#define CRC (&test_crc)
#define CRC_CR_RESET 1U
#define __HAL_RCC_CRC_CLK_ENABLE() (++crc_clocks)
#endif
