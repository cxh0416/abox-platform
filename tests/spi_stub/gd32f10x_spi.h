#ifndef TEST_SPI_GD_SPI_H
#define TEST_SPI_GD_SPI_H
#include "gd32f10x.h"
typedef struct { uint32_t device_mode,trans_mode,frame_size,nss,endian,clock_polarity_phase,prescale; } spi_parameter_struct;
#define SPI_MASTER 1U
#define SPI_SLAVE 0U
#define SPI_TRANSMODE_FULLDUPLEX 0U
#define SPI_TRANSMODE_RECEIVEONLY 1U
#define SPI_TRANSMODE_BDRECEIVE 2U
#define SPI_BIDIRECTIONAL_TRANSMIT 1U
#define SPI_BIDIRECTIONAL_RECEIVE 0U
#define SPI_FRAMESIZE_16BIT 16U
#define SPI_FRAMESIZE_8BIT 8U
#define SPI_NSS_SOFT 1U
#define SPI_NSS_HARD 0U
#define SPI_ENDIAN_LSB 1U
#define SPI_ENDIAN_MSB 0U
#define SPI_CK_PL_HIGH_PH_2EDGE 3U
#define SPI_CK_PL_HIGH_PH_1EDGE 2U
#define SPI_CK_PL_LOW_PH_2EDGE 1U
#define SPI_CK_PL_LOW_PH_1EDGE 0U
#define SPI_DMA_TRANSMIT 0U
#define SPI_DMA_RECEIVE 1U
extern uint32_t fake_spi_data[3];
#define SPI_DATA(id) fake_spi_data[(id)-1U]
#define SPI_FLAG_TBE 1U
#define SPI_FLAG_RBNE 2U
#define SPI_FLAG_TRANS 3U
#define SPI_FLAG_CONFERR 4U
#define SPI_FLAG_RXORERR 5U
void spi_struct_para_init(spi_parameter_struct *p);
void spi_i2s_deinit(uint32_t id);
void spi_init(uint32_t id, spi_parameter_struct *p);
void spi_nss_output_enable(uint32_t id);
void spi_enable(uint32_t id);
void spi_dma_enable(uint32_t id,uint8_t mode);
void spi_dma_disable(uint32_t id,uint8_t mode);
void spi_disable(uint32_t id);
void spi_bidirectional_transfer_config(uint32_t id,uint32_t direction);
FlagStatus spi_i2s_flag_get(uint32_t id, uint32_t flag);
void spi_i2s_data_transmit(uint32_t id, uint16_t value);
uint16_t spi_i2s_data_receive(uint32_t id);
#endif
