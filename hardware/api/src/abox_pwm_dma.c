#include "abox_pwm_dma_backend.h"
#include "abox_timer.h"
#include "abox_dma.h"

typedef struct {
    const ABoxPwmDmaEvents *events;
    ABoxDmaEvents dma_events;
    uint8_t instance, channel, controller, dma_channel, opened, circular;
    volatile uint8_t fault;
} PwmDma;

/* An update request is shared by all PWM channels of a timer. */
static PwmDma streams[5];
static void dma_event(void *context, unsigned controller, unsigned channel, ABoxDmaEvent event)
{
    PwmDma *s = (PwmDma *)context;
    const ABoxPwmDmaEvents *events = s->events;
    ABoxPwmDmaEvent result;
    (void)controller; (void)channel;
    if (s->fault) return;
    if (event == ABOX_DMA_TRANSFER_ERROR) {
        s->fault = 1U; ABoxPwmHw_DmaPause(s->instance); result = ABOX_PWM_DMA_ERROR;
    } else if (event == ABOX_DMA_COMPLETE) {
        if (!s->circular) ABoxPwmHw_DmaPause(s->instance);
        result = ABOX_PWM_DMA_COMPLETE;
    } else result = ABOX_PWM_DMA_HALF;
    if (events && events->on_event)
        events->on_event(events->context, s->instance, s->channel, result);
}

ABoxHalStatus ABoxPwm_StartDma(unsigned instance, unsigned channel, uint16_t initial,
                               const uint16_t *samples, uint16_t count, uint8_t circular,
                               const ABoxPwmDmaEvents *events)
{
    static const uint8_t request_channel[] = {5U, 2U, 3U, 7U, 2U};
    ABoxDmaConfig config = {ABOX_DMA_MEMORY_TO_PERIPHERAL, 2U, 2U, 0U, 1U, 0U, 1U};
    PwmDma *s;
    ABoxHalStatus status;
    if (instance < 1U || instance > 5U || channel < 1U || channel > 4U ||
        !samples || ((uintptr_t)samples & 1U) || !count || circular > 1U ||
        (events && !events->on_event)) return ABOX_HAL_INVALID;
    s = &streams[instance - 1U];
    if (s->instance) return ABOX_HAL_BUSY;
    status = ABoxPwmHw_DmaReserve(instance, channel, initial, samples, count);
    if (status != ABOX_HAL_OK) return status;
    s->instance = (uint8_t)instance; s->channel = (uint8_t)channel;
    s->controller = instance == 5U ? 2U : 1U;
    s->dma_channel = request_channel[instance - 1U];
    s->opened = 0U; s->circular = circular; s->fault = 0U; s->events = events;
    s->dma_events.context = s; s->dma_events.on_event = dma_event; config.circular = circular;
    status = ABoxDma_Open(s->controller, s->dma_channel, &config);
    if (status != ABOX_HAL_OK) goto failed;
    s->opened = 1U;
    status = ABoxDma_Start(s->controller, s->dma_channel, (uintptr_t)samples,
                           ABoxPwmHw_DmaAddress(instance, channel), count, &s->dma_events);
    if (status != ABOX_HAL_OK) goto failed;
    status = ABoxPwmHw_DmaPrepare(instance, channel, initial);
    if (status != ABOX_HAL_OK) {
        if (ABoxDma_Abort(s->controller, s->dma_channel) != ABOX_HAL_OK) return ABOX_HAL_ERROR;
        goto failed;
    }
    ABoxPwmHw_DmaEnable(instance);
    return ABOX_HAL_OK;
failed:
    if (!s->opened || ABoxDma_Close(s->controller, s->dma_channel) == ABOX_HAL_OK) {
        ABoxPwmHw_DmaRelease(instance, channel); s->instance = 0U; s->events = 0;
    }
    return status;
}

ABoxHalStatus ABoxPwm_StopDma(unsigned instance, unsigned channel)
{
    PwmDma *s;
    ABoxHalStatus status;
    uint8_t fault;
    if (instance < 1U || instance > 5U || channel < 1U || channel > 4U) return ABOX_HAL_INVALID;
    s = &streams[instance - 1U];
    if (s->instance != instance || s->channel != channel) return ABOX_HAL_INVALID;
    ABoxPwmHw_DmaPause(instance); fault = s->fault;
    if (s->opened) {
        status = ABoxDma_Abort(s->controller, s->dma_channel);
        if (status != ABOX_HAL_OK) return status;
        status = ABoxDma_Close(s->controller, s->dma_channel);
        if (status != ABOX_HAL_OK) return status;
    }
    ABoxPwmHw_DmaRelease(instance, channel); s->instance = 0U; s->events = 0;
    return fault ? ABOX_HAL_ERROR : ABOX_HAL_OK;
}
