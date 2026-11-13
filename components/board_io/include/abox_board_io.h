#ifndef ABOX_BOARD_IO_H
#define ABOX_BOARD_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ABOX_BOARD_IO_OUT1 = 0,
    ABOX_BOARD_IO_OUT2,
    ABOX_BOARD_IO_OUT3,
    ABOX_BOARD_IO_RELAY,
    ABOX_BOARD_IO_OUTPUT_COUNT
} ABoxBoardIoOutput;

typedef enum {
    ABOX_BOARD_IO_IN1 = 0,
    ABOX_BOARD_IO_IN2,
    ABOX_BOARD_IO_IN3,
    ABOX_BOARD_IO_IN4,
    ABOX_BOARD_IO_IN5,
    ABOX_BOARD_IO_IN6,
    ABOX_BOARD_IO_IN7,
    ABOX_BOARD_IO_IN8,
    ABOX_BOARD_IO_INPUT_COUNT
} ABoxBoardIoInput;

/* External connector positions, not MCU port B pin numbers. */
typedef enum {
    ABOX_BOARD_TERMINAL_B4 = 4, ABOX_BOARD_TERMINAL_B5,
    ABOX_BOARD_TERMINAL_B6, ABOX_BOARD_TERMINAL_B7,
    ABOX_BOARD_TERMINAL_B8, ABOX_BOARD_TERMINAL_B9,
    ABOX_BOARD_TERMINAL_B10, ABOX_BOARD_TERMINAL_B11
} ABoxBoardTerminal;

/* Instantaneous input level for a physical connector terminal. The selected
 * board returns zero for unsupported terminals or a null output pointer;
 * on failure it does not change *level. Initialize board inputs first. */
int ABoxBoardIo_ReadTerminal(ABoxBoardTerminal terminal, uint8_t *level);

typedef enum {
    ABOX_BOARD_IO_LED1 = 0,
    ABOX_BOARD_IO_LED2,
    ABOX_BOARD_IO_LED3,
    ABOX_BOARD_IO_LED_COUNT
} ABoxBoardIoLed;

typedef enum {
    ABOX_BOARD_IO_OK = 0,
    ABOX_BOARD_IO_NOT_READY,
    ABOX_BOARD_IO_INVALID_CHANNEL,
    ABOX_BOARD_IO_DENIED,
    ABOX_BOARD_IO_READBACK_FAILED,
    ABOX_BOARD_IO_IO_FAILED
} ABoxBoardIoResult;

typedef enum {
    ABOX_BOARD_IO_LED_OFF = 0,
    ABOX_BOARD_IO_LED_ON,
    ABOX_BOARD_IO_LED_BLINK
} ABoxBoardIoLedMode;

/* All electrical levels are active high. prepare must write low before
 * configuring board-owned outputs and must leave them low on failure. */
typedef struct {
    void *context;
    int (*prepare)(void *context);
    int (*write_output)(void *context, ABoxBoardIoOutput output, uint8_t level);
    int (*read_output)(void *context, ABoxBoardIoOutput output, uint8_t *level);
    int (*read_input)(void *context, ABoxBoardIoInput input, uint8_t *level);
    int (*write_led)(void *context, ABoxBoardIoLed led, uint8_t level);
    uint32_t (*enter_critical)(void *context);
    void (*exit_critical)(void *context, uint32_t key);
    /* Optional LED-only preparation. Must not touch inputs or power outputs. */
    int (*prepare_leds)(void *context, uint8_t led_mask);
    /* Optional instantaneous MCU pin level; distinct from the output latch.
     * Neither reading confirms the state of an external load. */
    int (*read_output_pin)(void *context, ABoxBoardIoOutput output, uint8_t *level);
} ABoxBoardIoPort;

/* Supplied by the selected hardware target after board GPIO initialization. */
const ABoxBoardIoPort *ABoxBoardIo_HardwarePort(void);

typedef struct {
    uint8_t raw_mask;
    uint8_t stable_mask;
    uint8_t valid_mask;
    uint32_t sampled_at_ms;
} ABoxBoardIoInputs;

typedef struct {
    const ABoxBoardIoPort *port;
    ABoxBoardIoInputs inputs;
    uint32_t changed_at_ms[ABOX_BOARD_IO_INPUT_COUNT];
    uint32_t led_changed_at_ms[ABOX_BOARD_IO_LED_COUNT];
    uint32_t led_half_period_ms[ABOX_BOARD_IO_LED_COUNT];
    uint8_t allowed_outputs;
    uint8_t candidate_mask;
    uint8_t observed_mask;
    uint8_t led_mode[ABOX_BOARD_IO_LED_COUNT];
    uint8_t led_level[ABOX_BOARD_IO_LED_COUNT];
    uint8_t ready;
    uint8_t full_io;
    uint8_t managed_leds;
    uint32_t led_on_ms[ABOX_BOARD_IO_LED_COUNT];
    uint32_t led_off_ms[ABOX_BOARD_IO_LED_COUNT];
    uint32_t led_pause_ms[ABOX_BOARD_IO_LED_COUNT];
    uint8_t led_pulses[ABOX_BOARD_IO_LED_COUNT];
} ABoxBoardIo;

/* allowed_outputs uses output enum bit positions. Disabled outputs may still
 * be commanded off. A successful output write confirms only the MCU latch. */
ABoxBoardIoResult ABoxBoardIo_Init(ABoxBoardIo *io, const ABoxBoardIoPort *port,
                                   uint8_t allowed_outputs);
/* Own only the selected LEDs. Output/input APIs remain NOT_READY. */
ABoxBoardIoResult ABoxBoardIo_LedsInit(ABoxBoardIo *io, const ABoxBoardIoPort *port,
                                      uint8_t led_mask);
/* Repeated on/off pulses followed by pause; total cycle <= INT32_MAX.
 * Calling with the same pattern preserves phase. Poll tolerates tick wrap. */
ABoxBoardIoResult ABoxBoardIo_LedPattern(ABoxBoardIo *io, ABoxBoardIoLed led,
    uint32_t on_ms, uint32_t off_ms, uint8_t pulses, uint32_t pause_ms, uint32_t now_ms);
ABoxBoardIoResult ABoxBoardIo_OutputSet(ABoxBoardIo *io, ABoxBoardIoOutput output,
                                        uint8_t on);
ABoxBoardIoResult ABoxBoardIo_OutputGet(ABoxBoardIo *io, ABoxBoardIoOutput output,
                                        uint8_t *on);
ABoxBoardIoResult ABoxBoardIo_InputsGet(ABoxBoardIo *io, ABoxBoardIoInputs *inputs);
ABoxBoardIoResult ABoxBoardIo_LedSet(ABoxBoardIo *io, ABoxBoardIoLed led,
                                     ABoxBoardIoLedMode mode, uint32_t period_ms,
                                     uint32_t now_ms);
void ABoxBoardIo_Poll(ABoxBoardIo *io, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
