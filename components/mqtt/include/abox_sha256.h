#ifndef ABOX_SHA256_H
#define ABOX_SHA256_H

#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_length;
    uint8_t block[64];
    uint8_t block_length;
} ABoxSha256;

void ABoxSha256_Init(ABoxSha256 *ctx);
void ABoxSha256_Update(ABoxSha256 *ctx, const uint8_t *data, uint32_t length);
void ABoxSha256_Final(ABoxSha256 *ctx, uint8_t digest[32]);

#endif
