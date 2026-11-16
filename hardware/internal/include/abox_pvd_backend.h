#ifndef ABOX_PVD_BACKEND_H
#define ABOX_PVD_BACKEND_H
#include "abox_pvd.h"
void ABoxPvdHw_EnableClock(void);
int ABoxPvdHw_IsEnabled(void);
void ABoxPvdHw_Configure(const ABoxPvdConfig *config);
void ABoxPvdHw_Disable(void);
uint8_t ABoxPvdHw_Read(void);
uint8_t ABoxPvdHw_Pending(void);
void ABoxPvdHw_Clear(void);
#endif
