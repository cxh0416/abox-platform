#include "abox_can_board_backend.h"
static ABoxCanBoardOps board;
int ABoxCanPort_BindBoard(const ABoxCanBoardOps *ops)
{
    if(ABoxCanPort_IsReady(0U) || ABoxCanPort_IsReady(1U) ||
       (ops && (!ops->prepare || !ops->release)))return 0;
    board=ops ? *ops : (ABoxCanBoardOps){0};return 1;
}
int ABoxCanBoard_Prepare(unsigned bus,ABoxCanMode mode)
{ return !board.prepare || board.prepare(board.context,bus,mode); }
void ABoxCanBoard_Release(unsigned bus,ABoxCanMode mode)
{ if(board.release)board.release(board.context,bus,mode); }
