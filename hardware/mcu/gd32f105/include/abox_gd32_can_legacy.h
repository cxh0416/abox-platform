#ifndef ABOX_GD32_CAN_LEGACY_H
#define ABOX_GD32_CAN_LEGACY_H

/* Transitional adapter for existing F105 applications using the STM32 HAL
 * CAN callback contract. The implementation is backed by the public CAN
 * event service and the GD32 controller, not by STM32 vendor drivers. */
#include <stdint.h>
#include "gd32f10x.h"
#include "abox_can_events.h"

typedef struct { volatile uint32_t TIR, TDTR, TDLR, TDHR; } ABoxGdCanMailbox;
typedef struct {
    volatile uint32_t MCR, MSR, TSR, RF0R, RF1R, IER, ESR, BTR;
    uint32_t reserved[88];
    ABoxGdCanMailbox sTxMailBox[3];
} ABoxGdCanRegisters;
typedef struct { ABoxGdCanRegisters *Instance; uint32_t ErrorCode; } CAN_HandleTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, TransmitGlobalTime; } CAN_TxHeaderTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_RxHeaderTypeDef;

#define CAN1 ((ABoxGdCanRegisters *)(uintptr_t)CAN_BASE)
#define CAN2 ((ABoxGdCanRegisters *)(uintptr_t)(CAN_BASE + 0x400U))
#define HAL_OK 0U
#define HAL_ERROR 1U
#define CAN_ID_STD 0U
#define CAN_ID_EXT 4U
#define CAN_RTR_DATA 0U
#define CAN_RTR_REMOTE 2U
#define CAN_RX_FIFO0 0U
#define CAN_TX_MAILBOX0 1U
#define CAN_TX_MAILBOX1 2U
#define CAN_TX_MAILBOX2 4U
#define CAN_TSR_RQCP0 1U
#define CAN_TSR_TXOK0 2U
#define CAN_TSR_ALST0 4U
#define CAN_TSR_TERR0 8U
#define CAN_FLAG_RQCP0 1U
#define CAN_FLAG_RQCP1 (1U << 8U)
#define CAN_FLAG_RQCP2 (1U << 16U)
#define CAN_ESR_EWGF 1U
#define CAN_ESR_EPVF 2U
#define CAN_ESR_BOFF 4U
#define CAN_MSR_ERRI 4U
#define CAN_IT_TX_MAILBOX_EMPTY (1U << 0U)
#define CAN_IT_RX_FIFO0_MSG_PENDING (1U << 1U)
#define CAN_IT_ERROR_WARNING (1U << 8U)
#define CAN_IT_ERROR_PASSIVE (1U << 9U)
#define CAN_IT_BUSOFF (1U << 10U)
#define CAN_IT_LAST_ERROR_CODE (1U << 11U)
#define CAN_IT_ERROR (1U << 15U)
#define HAL_CAN_ERROR_NONE 0U
#define HAL_CAN_ERROR_BOF ABOX_CAN_ERROR_BUS_OFF
#define __HAL_CAN_CLEAR_FLAG(h, flag) ((h)->Instance->TSR = (flag))

extern CAN_HandleTypeDef hcan1, hcan2;
void Error_Handler(void);
void CAN_Resume(void);
void CAN_EnterStandby(void);
uint32_t HAL_CAN_Start(CAN_HandleTypeDef *h);
uint32_t HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t notifications);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h);
uint32_t HAL_CAN_IsTxMessagePending(CAN_HandleTypeDef *h, uint32_t mailboxes);
uint32_t HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *header,
                              const uint8_t *data, uint32_t *mailbox);
uint32_t HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailboxes);
uint32_t HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo,
                              CAN_RxHeaderTypeDef *header, uint8_t *data);
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h);
void HAL_CAN_IRQHandler(CAN_HandleTypeDef *h);
void CanDiag_ObserveTx(CAN_HandleTypeDef *h, uint32_t tsr);
void HAL_CAN_DiagnosticSnapshot(CAN_HandleTypeDef *h, uint32_t ier, uint32_t msr, uint32_t tsr, uint32_t esr);
void HAL_CAN_DiagnosticTxReuse(CAN_HandleTypeDef *h, uint32_t tsr, uint32_t mailbox);
void HAL_CAN_DiagnosticTxAbort(CAN_HandleTypeDef *h, uint32_t tsr);
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h);
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *h);
void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *h);
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *h);
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *h);
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *h);
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *h);
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *h);

#endif
