#ifndef ABOX_EXTI_H
#define ABOX_EXTI_H
#include <stdint.h>
#include "abox_gpio.h"
#include "abox_hal_status.h"

typedef enum { ABOX_EXTI_RISING, ABOX_EXTI_FALLING, ABOX_EXTI_BOTH } ABoxExtiEdge;
typedef struct { void *context; void (*on_edge)(void *context, ABoxGpioPin pin); } ABoxExtiEvents;
#define ABOX_EXTI_INTERRUPT 1U
#define ABOX_EXTI_EVENT     2U
typedef struct { ABoxExtiEdge edge;uint8_t routes; } ABoxExtiConfig;

/* GPIO EXTI lines 0..15 are shared by the same pin number on all ports.
 * Configure the GPIO input separately and reserve the line, IRQ and AFIO
 * mapping against other users. The caller owns NVIC setup/priority and must
 * serialize all Open/Close operations and the relevant IRQs. A copy of events is retained until
 * Close; its context must remain valid. Callbacks run in IRQ context and
 * must not reenter the same line. Multiple edges can coalesce into one IRQ;
 * a callback is neither an edge counter nor proof of a stable input level. */
ABoxHalStatus ABoxExti_Open(ABoxGpioPin pin, ABoxExtiEdge edge, const ABoxExtiEvents *events);
/* Configure interrupt, event (e.g. WFE wake), or both. Event-only mode takes
 * a null event table and does not run an IRQ callback. IRQ mode requires one.
 * GPIO and the CPU/scheduler's WFE entry sequencing remain caller-owned. */
ABoxHalStatus ABoxExti_OpenConfigured(ABoxGpioPin pin,const ABoxExtiConfig *config,const ABoxExtiEvents *events);
ABoxHalStatus ABoxExti_Close(ABoxGpioPin pin);
/* Generate a software interrupt for an open interrupt-enabled line. As with
 * hardware edges, pending requests coalesce until the IRQ acknowledges them. */
ABoxHalStatus ABoxExti_Trigger(ABoxGpioPin pin);
/* Dispatch the line(s) belonging to the current IRQ, including shared IRQs. */
void ABoxExti_Irq(uint16_t lines);
/* Internal F105 EXTI sources: 16 PVD, 17 RTC alarm, 18 USB wakeup.
 * This controls the EXTI route only; configure and acknowledge the originating
 * peripheral separately. In particular, an RTC alarm IRQ can defer RTC flag
 * processing to task context. Coordinate with ABoxPvd, RTC and USB owners. */
typedef struct { void *context;void (*on_event)(void *context,unsigned line); } ABoxExtiInternalEvents;
ABoxHalStatus ABoxExtiInternal_Open(unsigned line,const ABoxExtiConfig *config,const ABoxExtiInternalEvents *events);
ABoxHalStatus ABoxExtiInternal_Close(unsigned line);
ABoxHalStatus ABoxExtiInternal_Trigger(unsigned line);
ABoxHalStatus ABoxExtiInternal_Pending(unsigned line,uint8_t *pending);
ABoxHalStatus ABoxExtiInternal_Clear(unsigned line);
void ABoxExtiInternal_Irq(uint32_t lines);
#endif
