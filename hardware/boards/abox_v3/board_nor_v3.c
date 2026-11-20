#include "abox_board_v3_nor.h"
#include "gd32f10x_spi.h"
#include "gd32f10x.h"
#include <stddef.h>

static void select_chip(void *context, int selected)
{
    (void)context;
    gpio_bit_write(GPIOA, GPIO_PIN_4, selected ? RESET : SET);
}

static int wait_flag(uint32_t flag, uint32_t value)
{
    for (unsigned spin = 0; spin < 2048U; ++spin)
        if ((SPI_STAT(SPI0) & flag) == value) return 1;
    return 0;
}

static int transfer(void *context, const uint8_t *tx, uint8_t *rx, uint32_t length)
{
    (void)context;
    for (uint32_t i = 0; i < length; ++i) {
        if (!wait_flag(SPI_STAT_TBE, SPI_STAT_TBE)) return 0;
        SPI_DATA(SPI0) = tx ? tx[i] : 0xFFU;
        if (!wait_flag(SPI_STAT_RBNE, SPI_STAT_RBNE)) return 0;
        uint8_t value = (uint8_t)SPI_DATA(SPI0);
        if (rx) rx[i] = value;
    }
    return wait_flag(SPI_STAT_TRANS, 0U);
}

int ABoxBoardV3_NorOpen(ABoxNor *nor, uint32_t (*tick_ms)(void *), uint32_t *jedec_id)
{
    const ABoxNorSpi spi = {NULL, select_chip, transfer, tick_ms};
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_AF);
    gpio_bit_set(GPIOA, GPIO_PIN_4);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_4);
    AFIO_PCF0 = (AFIO_PCF0 & ~(AFIO_PCF0_SWJ_CFG | AFIO_PCF0_SPI0_REMAP)) | (2UL << 24);
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5 | GPIO_PIN_7);
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, GPIO_PIN_6);
    rcu_periph_clock_enable(RCU_SPI0);
    rcu_periph_reset_enable(RCU_SPI0RST);
    rcu_periph_reset_disable(RCU_SPI0RST);
    /* SPI mode 0, software NSS, APB2 / 8. */
    SPI_CTL0(SPI0) = SPI_CTL0_MSTMOD | SPI_CTL0_SWNSSEN | SPI_CTL0_SWNSS |
        (2U << 3) | SPI_CTL0_SPIEN;
    return ABoxNor_Open(nor, &spi, jedec_id);
}
