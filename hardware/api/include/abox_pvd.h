#ifndef ABOX_PVD_H
#define ABOX_PVD_H
#include <stdint.h>
#include "abox_hal_status.h"

/* Nominal F105 threshold selection; actual switching level and hysteresis are
 * device-dependent electrical characteristics, not a voltage measurement. */
typedef enum { ABOX_PVD_2V2,ABOX_PVD_2V3,ABOX_PVD_2V4,ABOX_PVD_2V5,
               ABOX_PVD_2V6,ABOX_PVD_2V7,ABOX_PVD_2V8,ABOX_PVD_2V9 } ABoxPvdLevel;
typedef enum { ABOX_PVD_RISING,ABOX_PVD_FALLING,ABOX_PVD_BOTH } ABoxPvdEdge;
typedef struct { ABoxPvdLevel level;ABoxPvdEdge edge;uint8_t interrupt,event; } ABoxPvdConfig;
typedef struct { void *context;void (*changed)(void *context,uint8_t below); } ABoxPvdEvents;

/* Exclusively owns the detector and EXTI line 16 until Close. The board owns
 * NVIC and dispatches PVD/LVD IRQ to ABoxPvd_Irq; event-only or polling use
 * need no IRQ. Rising/falling refer to the detector output: below=1 means
 * supply below the selected threshold. On an edge the callback reports the
 * current sampled level, not a guaranteed history of all transitions. Events
 * and context remain valid through Close; callbacks must not reenter this API.
 * Coordinate other PWR/PMU register and EXTI users, including RTC. */
ABoxHalStatus ABoxPvd_Open(const ABoxPvdConfig *config,const ABoxPvdEvents *events);
ABoxHalStatus ABoxPvd_Read(uint8_t *below);
ABoxHalStatus ABoxPvd_Pending(uint8_t *pending);
ABoxHalStatus ABoxPvd_Clear(void);
void ABoxPvd_Irq(void);
ABoxHalStatus ABoxPvd_Close(void);
#endif
