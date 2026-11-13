#ifndef TEST_STM32_CAN_H
#define TEST_STM32_CAN_H
#include <stdint.h>
#ifndef HAL_OK
#define HAL_OK 0U
#endif
#define HAL_ERROR 1U
#define HAL_CAN_STATE_RESET 0U
#define HAL_CAN_STATE_READY 1U
#define HAL_CAN_STATE_LISTENING 2U
#define CAN1 ((void *)0x1000U)
#define CAN2 ((void *)0x2000U)
#define CAN_MODE_NORMAL 0U
#define CAN_MODE_SILENT_LOOPBACK 3U
#define CAN_SJW_1TQ 0U
#define CAN_BS1_15TQ 14U
#define CAN_BS2_2TQ 1U
#define DISABLE 0U
#define ENABLE 1U
#define CAN_FILTERMODE_IDMASK 0U
#define CAN_FILTERSCALE_32BIT 1U
#define CAN_FILTER_FIFO0 0U
#define CAN_RX_FIFO0 0U
#define CAN_ID_STD 0U
#define CAN_ID_EXT 4U
#define CAN_RTR_DATA 0U
#define CAN_RTR_REMOTE 2U
typedef struct {
    uint32_t Prescaler, Mode, SyncJumpWidth, TimeSeg1, TimeSeg2, TimeTriggeredMode,
        AutoBusOff, AutoWakeUp, AutoRetransmission, ReceiveFifoLocked, TransmitFifoPriority;
} CAN_InitTypeDef;
typedef struct { void *Instance; CAN_InitTypeDef Init; uint32_t State; } CAN_HandleTypeDef;
typedef struct {
    uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow,
        FilterFIFOAssignment, FilterBank, FilterMode, FilterScale, FilterActivation, SlaveStartFilterBank;
} CAN_FilterTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, TransmitGlobalTime; } CAN_TxHeaderTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_RxHeaderTypeDef;
extern CAN_HandleTypeDef hcan1, hcan2;
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_CAN_Init(CAN_HandleTypeDef *h);
uint32_t HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *filter);
uint32_t HAL_CAN_Start(CAN_HandleTypeDef *h);
uint32_t HAL_CAN_Stop(CAN_HandleTypeDef *h);
uint32_t HAL_CAN_DeInit(CAN_HandleTypeDef *h);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h);
uint32_t HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *header, uint8_t data[8], uint32_t *mailbox);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo);
uint32_t HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t data[8]);
#endif
