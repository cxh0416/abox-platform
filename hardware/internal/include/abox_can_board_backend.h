#ifndef ABOX_CAN_BOARD_BACKEND_H
#define ABOX_CAN_BOARD_BACKEND_H
#include "abox_can_port.h"
int ABoxCanBoard_Prepare(unsigned bus,ABoxCanMode mode);
void ABoxCanBoard_Release(unsigned bus,ABoxCanMode mode);
#endif
