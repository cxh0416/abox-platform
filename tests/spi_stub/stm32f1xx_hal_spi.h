#include "stm32f1xx_hal.h"
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle, const uint8_t *tx,
                                         uint8_t *rx, uint16_t count, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, const uint8_t *tx,
                                  uint16_t count, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *handle, uint8_t *rx,
                                 uint16_t count, uint32_t timeout);
