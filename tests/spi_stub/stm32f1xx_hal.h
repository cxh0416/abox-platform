#ifndef TEST_SPI_STM_HAL_H
#define TEST_SPI_STM_HAL_H
#include <stdint.h>
extern uint32_t fake_clock_regs[12];
#define RCC ((void *)fake_clock_regs)
#define HSE_VALUE 8000000U
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { unsigned id; uint32_t CR1, CR2, SR, DR; } SPI_TypeDef;
#define SPI_CR2_RXDMAEN 1U
#define SPI_CR2_TXDMAEN 2U
#define SPI_SR_TXE 2U
#define SPI_SR_MODF 32U
#define SPI_SR_OVR 64U
#define SPI_SR_BSY 128U
#define __HAL_SPI_ENABLE(h) ((h)->Instance->CR1 |= 1U)
#define __HAL_SPI_DISABLE(h) ((h)->Instance->CR1 &= ~1U)
#define SPI_1LINE_TX(h) ((h)->Instance->CR1 |= 2U)
#define SPI_1LINE_RX(h) ((h)->Instance->CR1 &= ~2U)
typedef struct {
    uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS;
    uint32_t BaudRatePrescaler, FirstBit, TIMode, CRCCalculation, CRCPolynomial;
} SPI_InitTypeDef;
typedef struct { SPI_TypeDef *Instance; SPI_InitTypeDef Init; } SPI_HandleTypeDef;
extern SPI_TypeDef fake_spi_st[3];
#define SPI1 (&fake_spi_st[0])
#define SPI2 (&fake_spi_st[1])
#define SPI3 (&fake_spi_st[2])
#define SPI_MODE_MASTER 1U
#define SPI_MODE_SLAVE 0U
#define SPI_DIRECTION_2LINES 0U
#define SPI_DIRECTION_2LINES_RXONLY 1U
#define SPI_DIRECTION_1LINE 2U
#define SPI_DATASIZE_8BIT 8U
#define SPI_DATASIZE_16BIT 16U
#define SPI_POLARITY_HIGH 2U
#define SPI_POLARITY_LOW 0U
#define SPI_PHASE_2EDGE 1U
#define SPI_PHASE_1EDGE 0U
#define SPI_NSS_SOFT 1U
#define SPI_NSS_HARD_OUTPUT 2U
#define SPI_NSS_HARD_INPUT 0U
#define SPI_CR1_BR_Pos 3U
#define SPI_FIRSTBIT_LSB 1U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_TIMODE_DISABLE 0U
#define SPI_CRCCALCULATION_DISABLE 0U
void fake_spi_clock(unsigned id, unsigned enabled);
#define __HAL_RCC_SPI1_CLK_ENABLE() fake_spi_clock(1,1)
#define __HAL_RCC_SPI2_CLK_ENABLE() fake_spi_clock(2,1)
#define __HAL_RCC_SPI3_CLK_ENABLE() fake_spi_clock(3,1)
#define __HAL_RCC_SPI1_CLK_DISABLE() fake_spi_clock(1,0)
#define __HAL_RCC_SPI2_CLK_DISABLE() fake_spi_clock(2,0)
#define __HAL_RCC_SPI3_CLK_DISABLE() fake_spi_clock(3,0)
#endif
