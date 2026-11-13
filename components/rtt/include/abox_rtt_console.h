#ifndef ABOX_RTT_CONSOLE_H
#define ABOX_RTT_CONSOLE_H

#include <stdint.h>
#include "abox_ec800_at.h"

/* Debug-only. All calls, including AT callbacks, must run in one context. */
#ifndef ABOX_RTT_CONSOLE_OUTPUT_SIZE
#define ABOX_RTT_CONSOLE_OUTPUT_SIZE 1536U
#endif
#define ABOX_RTT_CONSOLE_INPUT_SIZE 256U

typedef struct {
    ABoxEc800At *at;
    ABoxEc800Owner owner;
    const char *info;
    char input[ABOX_RTT_CONSOLE_INPUT_SIZE];
    uint8_t output[ABOX_RTT_CONSOLE_OUTPUT_SIZE];
    uint16_t input_len, output_head, output_count;
    uint32_t dropped;
    uint32_t line_overflow_before, rx_overflow_before;
    uint8_t input_overflow, pending, done, result, log_enabled;
} ABoxRttConsole;

/* owner must be unique and an AT handler slot must be available. info must
 * remain valid. Initialize the RTT transport separately before this call. */
int ABoxRttConsole_Init(ABoxRttConsole *c, ABoxEc800At *at,
                        ABoxEc800Owner owner, const char *info);
void ABoxRttConsole_Poll(ABoxRttConsole *c);
/* Submit already-formatted product logs from the same execution context. */
void ABoxRttConsole_Log(ABoxRttConsole *c, const char *line, uint16_t length);

#endif
