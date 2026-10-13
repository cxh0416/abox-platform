#include "abox_sha256.h"

#include <string.h>

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32U - (n))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define E0(x) (ROTR((x),2U) ^ ROTR((x),13U) ^ ROTR((x),22U))
#define E1(x) (ROTR((x),6U) ^ ROTR((x),11U) ^ ROTR((x),25U))
#define S0(x) (ROTR((x),7U) ^ ROTR((x),18U) ^ ((x) >> 3U))
#define S1(x) (ROTR((x),17U) ^ ROTR((x),19U) ^ ((x) >> 10U))

static const uint32_t k[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

static uint32_t load32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | p[3]; }

static void transform(ABoxSha256 *ctx, const uint8_t block[64])
{
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    uint8_t i;
    for (i=0U;i<16U;i++) w[i]=load32(block+(uint32_t)i*4U);
    for (i=16U;i<64U;i++) w[i]=S1(w[i-2U])+w[i-7U]+S0(w[i-15U])+w[i-16U];
    a=ctx->state[0];b=ctx->state[1];c=ctx->state[2];d=ctx->state[3];
    e=ctx->state[4];f=ctx->state[5];g=ctx->state[6];h=ctx->state[7];
    for(i=0U;i<64U;i++){t1=h+E1(e)+CH(e,f,g)+k[i]+w[i];t2=E0(a)+MAJ(a,b,c);h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

void ABoxSha256_Init(ABoxSha256 *ctx)
{
    static const uint32_t initial[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                      0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    memcpy(ctx->state, initial, sizeof(initial));
}

void ABoxSha256_Update(ABoxSha256 *ctx, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    if (!ctx || (!data && length)) return;
    for (i = 0U; i < length; i++) {
        ctx->block[ctx->block_length++] = data[i];
        if (ctx->block_length == 64U) {
            transform(ctx, ctx->block);
            ctx->bit_length += 512U;
            ctx->block_length = 0U;
        }
    }
}

void ABoxSha256_Final(ABoxSha256 *ctx, uint8_t digest[32])
{
    uint8_t i;uint64_t bits;if(!ctx||!digest)return;
    bits=ctx->bit_length+(uint64_t)ctx->block_length*8U;
    ctx->block[ctx->block_length++]=0x80U;
    if(ctx->block_length>56U){while(ctx->block_length<64U)ctx->block[ctx->block_length++]=0U;transform(ctx,ctx->block);ctx->block_length=0U;}
    while(ctx->block_length<56U)ctx->block[ctx->block_length++]=0U;
    for(i=0U;i<8U;i++)ctx->block[63U-i]=(uint8_t)(bits>>(uint32_t)i*8U);
    transform(ctx,ctx->block);
    for(i=0U;i<8U;i++){digest[i*4U]=(uint8_t)(ctx->state[i]>>24U);digest[i*4U+1U]=(uint8_t)(ctx->state[i]>>16U);digest[i*4U+2U]=(uint8_t)(ctx->state[i]>>8U);digest[i*4U+3U]=(uint8_t)ctx->state[i];}
    memset(ctx,0,sizeof(*ctx));
}
