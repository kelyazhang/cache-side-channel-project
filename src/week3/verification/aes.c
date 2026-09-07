#include "aes.h"

#include <string.h>

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint32_t rcon[10] = {
    0x01000000U, 0x02000000U, 0x04000000U, 0x08000000U, 0x10000000U,
    0x20000000U, 0x40000000U, 0x80000000U, 0x1b000000U, 0x36000000U
};

static inline uint32_t get_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) ^ ((uint32_t)p[1] << 16) ^
           ((uint32_t)p[2] << 8) ^ (uint32_t)p[3];
}

static inline void put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1bU));
}

static inline uint32_t sub_word(uint32_t x)
{
    return ((uint32_t)sbox[(x >> 24) & 0xffU] << 24) ^
           ((uint32_t)sbox[(x >> 16) & 0xffU] << 16) ^
           ((uint32_t)sbox[(x >> 8) & 0xffU] << 8) ^
           (uint32_t)sbox[x & 0xffU];
}

static inline uint32_t rot_word(uint32_t x)
{
    return (x << 8) | (x >> 24);
}

static inline uint32_t *table(uint32_t *tables, int table_id)
{
    return tables + (size_t)table_id * 256U;
}

static inline const uint32_t *const_table(const uint32_t *tables, int table_id)
{
    return tables + (size_t)table_id * 256U;
}

void aes_init_tables(uint32_t *tables)
{
    uint32_t *te0 = table(tables, 0);
    uint32_t *te1 = table(tables, 1);
    uint32_t *te2 = table(tables, 2);
    uint32_t *te3 = table(tables, 3);

    for (int x = 0; x < 256; ++x) {
        const uint8_t s = sbox[x];
        const uint8_t s2 = xtime(s);
        const uint8_t s3 = (uint8_t)(s2 ^ s);

        te0[x] = ((uint32_t)s2 << 24) | ((uint32_t)s << 16) |
                 ((uint32_t)s << 8) | s3;
        te1[x] = ((uint32_t)s3 << 24) | ((uint32_t)s2 << 16) |
                 ((uint32_t)s << 8) | s;
        te2[x] = ((uint32_t)s << 24) | ((uint32_t)s3 << 16) |
                 ((uint32_t)s2 << 8) | s;
        te3[x] = ((uint32_t)s << 24) | ((uint32_t)s << 16) |
                 ((uint32_t)s3 << 8) | s2;
    }
}

void aes_expand_key(aes_context_t *ctx, const uint8_t key[16])
{
    for (int i = 0; i < 4; ++i) {
        ctx->round_key[i] = get_u32_be(key + 4 * i);
    }

    for (int i = 4; i < 44; ++i) {
        uint32_t temp = ctx->round_key[i - 1];
        if ((i & 3) == 0) {
            temp = sub_word(rot_word(temp)) ^ rcon[(i / 4) - 1];
        }
        ctx->round_key[i] = ctx->round_key[i - 4] ^ temp;
    }
}

static __attribute__((noinline)) void ttable_round(
    const uint32_t *tables,
    uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3,
    const uint32_t *rk,
    uint32_t *s0, uint32_t *s1, uint32_t *s2, uint32_t *s3)
{
    const uint32_t *te0 = const_table(tables, 0);
    const uint32_t *te1 = const_table(tables, 1);
    const uint32_t *te2 = const_table(tables, 2);
    const uint32_t *te3 = const_table(tables, 3);

    *s0 = te0[(t0 >> 24) & 0xffU] ^ te1[(t1 >> 16) & 0xffU] ^
          te2[(t2 >> 8) & 0xffU] ^ te3[t3 & 0xffU] ^ rk[0];
    *s1 = te0[(t1 >> 24) & 0xffU] ^ te1[(t2 >> 16) & 0xffU] ^
          te2[(t3 >> 8) & 0xffU] ^ te3[t0 & 0xffU] ^ rk[1];
    *s2 = te0[(t2 >> 24) & 0xffU] ^ te1[(t3 >> 16) & 0xffU] ^
          te2[(t0 >> 8) & 0xffU] ^ te3[t1 & 0xffU] ^ rk[2];
    *s3 = te0[(t3 >> 24) & 0xffU] ^ te1[(t0 >> 16) & 0xffU] ^
          te2[(t1 >> 8) & 0xffU] ^ te3[t2 & 0xffU] ^ rk[3];
}

static void final_round(const aes_context_t *ctx,
                        uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3,
                        uint8_t out[16])
{
    uint32_t s0 = ((uint32_t)sbox[(t0 >> 24) & 0xffU] << 24) ^
                  ((uint32_t)sbox[(t1 >> 16) & 0xffU] << 16) ^
                  ((uint32_t)sbox[(t2 >> 8) & 0xffU] << 8) ^
                  sbox[t3 & 0xffU] ^ ctx->round_key[40];
    uint32_t s1 = ((uint32_t)sbox[(t1 >> 24) & 0xffU] << 24) ^
                  ((uint32_t)sbox[(t2 >> 16) & 0xffU] << 16) ^
                  ((uint32_t)sbox[(t3 >> 8) & 0xffU] << 8) ^
                  sbox[t0 & 0xffU] ^ ctx->round_key[41];
    uint32_t s2 = ((uint32_t)sbox[(t2 >> 24) & 0xffU] << 24) ^
                  ((uint32_t)sbox[(t3 >> 16) & 0xffU] << 16) ^
                  ((uint32_t)sbox[(t0 >> 8) & 0xffU] << 8) ^
                  sbox[t1 & 0xffU] ^ ctx->round_key[42];
    uint32_t s3 = ((uint32_t)sbox[(t3 >> 24) & 0xffU] << 24) ^
                  ((uint32_t)sbox[(t0 >> 16) & 0xffU] << 16) ^
                  ((uint32_t)sbox[(t1 >> 8) & 0xffU] << 8) ^
                  sbox[t2 & 0xffU] ^ ctx->round_key[43];

    put_u32_be(out + 0, s0);
    put_u32_be(out + 4, s1);
    put_u32_be(out + 8, s2);
    put_u32_be(out + 12, s3);
}

void aes_first_round(const aes_context_t *ctx,
                     const uint8_t in[16], uint32_t state_out[4])
{
    uint32_t t0 = get_u32_be(in + 0) ^ ctx->round_key[0];
    uint32_t t1 = get_u32_be(in + 4) ^ ctx->round_key[1];
    uint32_t t2 = get_u32_be(in + 8) ^ ctx->round_key[2];
    uint32_t t3 = get_u32_be(in + 12) ^ ctx->round_key[3];

    ttable_round(ctx->tables, t0, t1, t2, t3, &ctx->round_key[4],
                 &state_out[0], &state_out[1], &state_out[2], &state_out[3]);
}

void aes_finish_after_first_round(const aes_context_t *ctx,
                                  const uint32_t first_state[4],
                                  uint8_t out[16])
{
    uint32_t t0 = first_state[0];
    uint32_t t1 = first_state[1];
    uint32_t t2 = first_state[2];
    uint32_t t3 = first_state[3];

    for (int round = 2; round <= 9; ++round) {
        uint32_t s0, s1, s2, s3;
        ttable_round(ctx->tables, t0, t1, t2, t3,
                     &ctx->round_key[4 * round], &s0, &s1, &s2, &s3);
        t0 = s0; t1 = s1; t2 = s2; t3 = s3;
    }

    final_round(ctx, t0, t1, t2, t3, out);
}

void aes_encrypt_full(const aes_context_t *ctx,
                      const uint8_t in[16], uint8_t out[16])
{
    uint32_t first_state[4];
    aes_first_round(ctx, in, first_state);
    aes_finish_after_first_round(ctx, first_state, out);
}
