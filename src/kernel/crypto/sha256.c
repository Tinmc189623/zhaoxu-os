/*
 * Zhaoxu OS - SHA-256 (FIPS 180-4).
 *
 * Used to derive per-file eCryptfs keys from the mount passphrase and the
 * per-file salt.  The implementation is shared by the kernel and by Ring 3
 * code through the same freestanding source file.
 */
#include <crypto.h>
#include <string.h>
#include <types.h>

static const u32 g_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

struct sha256_ctx {
    u32 state[8];
    u64 bits;
    u8 buffer[64];
    u32 used;
};

static u32 rotr(u32 value, int count)
{
    return (value >> count) | (value << (32 - count));
}

static void sha256_block(struct sha256_ctx *ctx, const u8 *block)
{
    u32 w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((u32)block[i * 4] << 24) |
               ((u32)block[i * 4 + 1] << 16) |
               ((u32)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                 (w[i - 15] >> 3);
        u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                 (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = ctx->state[0];
    u32 b = ctx->state[1];
    u32 c = ctx->state[2];
    u32 d = ctx->state[3];
    u32 e = ctx->state[4];
    u32 f = ctx->state[5];
    u32 g = ctx->state[6];
    u32 h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        u32 s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = h + s1 + ch + g_k[i] + w[i];
        u32 s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(struct sha256_ctx *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->bits = 0;
    ctx->used = 0;
}

static void sha256_update(struct sha256_ctx *ctx, const u8 *data,
                          usize length)
{
    ctx->bits += (u64)length * 8;
    while (length) {
        u32 space = 64 - ctx->used;
        u32 chunk = length < space ? (u32)length : space;
        memcpy(ctx->buffer + ctx->used, data, chunk);
        ctx->used += chunk;
        data += chunk;
        length -= chunk;
        if (ctx->used == 64) {
            sha256_block(ctx, ctx->buffer);
            ctx->used = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx *ctx, u8 out[32])
{
    u64 bits = ctx->bits;
    u8 pad = 0x80;
    sha256_update(ctx, &pad, 1);
    u8 zero = 0;
    while (ctx->used != 56)
        sha256_update(ctx, &zero, 1);
    u8 length[8];
    for (int i = 0; i < 8; i++)
        length[i] = (u8)(bits >> (56 - i * 8));
    sha256_update(ctx, length, 8);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (u8)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (u8)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (u8)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (u8)ctx->state[i];
    }
}

void sha256(const void *data, usize length, u8 out[32])
{
    struct sha256_ctx ctx;
    sha256_init(&ctx);
    if (length)
        sha256_update(&ctx, data, length);
    sha256_final(&ctx, out);
}
