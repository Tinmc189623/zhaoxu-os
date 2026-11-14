#ifndef ZHAOXU_CRYPTO_H
#define ZHAOXU_CRYPTO_H

#include <types.h>

/* SHA-256.  `out` receives 32 bytes. */
void sha256(const void *data, usize length, u8 out[32]);

/* AES block cipher.  A context holds the expanded key schedule; the same
 * structure serves both directions.  Only 128-, 192- and 256-bit keys are
 * accepted. */
#define AES_BLOCK_SIZE 16
#define AES_MAX_ROUND_KEYS 60

struct aes_ctx {
    u32 round_keys[AES_MAX_ROUND_KEYS];
    int rounds;
};

int aes_set_key(struct aes_ctx *ctx, const u8 *key, usize key_len);
void aes_encrypt_block(const struct aes_ctx *ctx, const u8 in[16],
                       u8 out[16]);
void aes_decrypt_block(const struct aes_ctx *ctx, const u8 in[16],
                       u8 out[16]);

/* CBC mode with PKCS#7 padding.  `out` must have room for length + 16
 * bytes.  Returns the ciphertext length, or -1 on failure. */
int aes_cbc_encrypt(const struct aes_ctx *ctx, const u8 iv[16],
                    const u8 *plain, usize length, u8 *out, usize capacity);
int aes_cbc_decrypt(const struct aes_ctx *ctx, const u8 iv[16],
                    const u8 *cipher, usize length, u8 *out, usize capacity);

#endif
