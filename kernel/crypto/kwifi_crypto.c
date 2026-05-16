#include "crypto/kwifi_crypto.h"

typedef struct
{
    uint32_t h[5];
    uint64_t bits;
    uint8_t buf[64];
    uint32_t used;
} kwifi_sha1_ctx;

typedef struct
{
    uint32_t h[4];
    uint64_t bits;
    uint8_t buf[64];
    uint32_t used;
} kwifi_md4_ctx;

static uint32_t krotl32(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32u - n));
}

static uint32_t kload_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint32_t kload_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void kstore_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void kstore_le64(uint8_t *p, uint64_t v)
{
    for (uint32_t i = 0u; i < 8u; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
}

static void kstore_be64(uint8_t *p, uint64_t v)
{
    for (uint32_t i = 0u; i < 8u; ++i)
        p[i] = (uint8_t)(v >> ((7u - i) * 8u));
}

static void kzero(uint8_t *p, uint32_t len)
{
    for (uint32_t i = 0u; i < len; ++i)
        p[i] = 0u;
}

static void kcopy(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0u; i < len; ++i)
        dst[i] = src[i];
}

static uint32_t kcstr_len_cap(const char *s, uint32_t cap)
{
    uint32_t len = 0u;
    if (!s)
        return 0u;
    while (s[len] && len < cap)
        len++;
    return len;
}

static void sha1_transform(uint32_t h[5], const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;

    for (uint32_t i = 0u; i < 16u; ++i)
        w[i] = kload_be32(block + i * 4u);
    for (uint32_t i = 16u; i < 80u; ++i)
        w[i] = krotl32(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1u);

    a = h[0];
    b = h[1];
    c = h[2];
    d = h[3];
    e = h[4];

    for (uint32_t i = 0u; i < 80u; ++i)
    {
        uint32_t f;
        uint32_t k;
        uint32_t t;

        if (i < 20u)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        }
        else if (i < 40u)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60u)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }

        t = krotl32(a, 5u) + f + e + k + w[i];
        e = d;
        d = c;
        c = krotl32(b, 30u);
        b = a;
        a = t;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

static void sha1_init(kwifi_sha1_ctx *ctx)
{
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xEFCDAB89u;
    ctx->h[2] = 0x98BADCFEu;
    ctx->h[3] = 0x10325476u;
    ctx->h[4] = 0xC3D2E1F0u;
    ctx->bits = 0u;
    ctx->used = 0u;
}

static void sha1_update(kwifi_sha1_ctx *ctx, const uint8_t *data, uint32_t len)
{
    ctx->bits += (uint64_t)len * 8ull;
    while (len)
    {
        uint32_t chunk = 64u - ctx->used;
        if (chunk > len)
            chunk = len;
        kcopy(ctx->buf + ctx->used, data, chunk);
        ctx->used += chunk;
        data += chunk;
        len -= chunk;
        if (ctx->used == 64u)
        {
            sha1_transform(ctx->h, ctx->buf);
            ctx->used = 0u;
        }
    }
}

static void sha1_final(kwifi_sha1_ctx *ctx, uint8_t out[20])
{
    uint8_t pad[64];
    uint8_t bits[8];

    kzero(pad, sizeof(pad));
    pad[0] = 0x80u;
    kstore_be64(bits, ctx->bits);
    sha1_update(ctx, pad, (ctx->used < 56u) ? (56u - ctx->used) : (120u - ctx->used));
    sha1_update(ctx, bits, sizeof(bits));

    for (uint32_t i = 0u; i < 5u; ++i)
        kstore_be32(out + i * 4u, ctx->h[i]);
}

void kwifi_sha1(const uint8_t *data, uint32_t len, uint8_t out[20])
{
    kwifi_sha1_ctx ctx;
    sha1_init(&ctx);
    if (data && len)
        sha1_update(&ctx, data, len);
    sha1_final(&ctx, out);
}

void kwifi_hmac_sha1(const uint8_t *key,
                     uint32_t key_len,
                     const uint8_t *data,
                     uint32_t data_len,
                     uint8_t out[20])
{
    uint8_t kopad[64];
    uint8_t kipad[64];
    uint8_t khash[20];
    uint8_t inner[20];
    kwifi_sha1_ctx ctx;

    if (key_len > 64u)
    {
        kwifi_sha1(key, key_len, khash);
        key = khash;
        key_len = sizeof(khash);
    }

    for (uint32_t i = 0u; i < 64u; ++i)
    {
        uint8_t k = (key && i < key_len) ? key[i] : 0u;
        kipad[i] = (uint8_t)(k ^ 0x36u);
        kopad[i] = (uint8_t)(k ^ 0x5Cu);
    }

    sha1_init(&ctx);
    sha1_update(&ctx, kipad, sizeof(kipad));
    if (data && data_len)
        sha1_update(&ctx, data, data_len);
    sha1_final(&ctx, inner);

    sha1_init(&ctx);
    sha1_update(&ctx, kopad, sizeof(kopad));
    sha1_update(&ctx, inner, sizeof(inner));
    sha1_final(&ctx, out);
}

void kwifi_wpa_prf_sha1(const uint8_t *key,
                        uint32_t key_len,
                        const char *label,
                        const uint8_t *data,
                        uint32_t data_len,
                        uint8_t *out,
                        uint32_t out_len)
{
    uint8_t input[192];
    uint8_t digest[20];
    uint32_t label_len = kcstr_len_cap(label, 96u);
    uint32_t pos = 0u;
    uint8_t counter = 0u;

    if (!out || !key || !label || label_len + 2u + data_len > sizeof(input))
        return;

    while (pos < out_len)
    {
        uint32_t chunk;
        kcopy(input, (const uint8_t *)label, label_len);
        input[label_len] = 0u;
        if (data && data_len)
            kcopy(input + label_len + 1u, data, data_len);
        input[label_len + 1u + data_len] = counter;

        kwifi_hmac_sha1(key, key_len, input, label_len + 2u + data_len, digest);
        chunk = out_len - pos;
        if (chunk > sizeof(digest))
            chunk = sizeof(digest);
        kcopy(out + pos, digest, chunk);
        pos += chunk;
        counter++;
    }
}

static void md4_transform(uint32_t h[4], const uint8_t block[64])
{
    uint32_t x[16];
    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];

    for (uint32_t i = 0u; i < 16u; ++i)
        x[i] = kload_le32(block + i * 4u);

#define F(xv, yv, zv) (((xv) & (yv)) | (~(xv) & (zv)))
#define G(xv, yv, zv) (((xv) & (yv)) | ((xv) & (zv)) | ((yv) & (zv)))
#define H(xv, yv, zv) ((xv) ^ (yv) ^ (zv))
#define R1(a_, b_, c_, d_, k_, s_) a_ = krotl32((a_) + F((b_), (c_), (d_)) + x[(k_)], (s_))
#define R2(a_, b_, c_, d_, k_, s_) a_ = krotl32((a_) + G((b_), (c_), (d_)) + x[(k_)] + 0x5A827999u, (s_))
#define R3(a_, b_, c_, d_, k_, s_) a_ = krotl32((a_) + H((b_), (c_), (d_)) + x[(k_)] + 0x6ED9EBA1u, (s_))

    R1(a, b, c, d, 0, 3);  R1(d, a, b, c, 1, 7);  R1(c, d, a, b, 2, 11); R1(b, c, d, a, 3, 19);
    R1(a, b, c, d, 4, 3);  R1(d, a, b, c, 5, 7);  R1(c, d, a, b, 6, 11); R1(b, c, d, a, 7, 19);
    R1(a, b, c, d, 8, 3);  R1(d, a, b, c, 9, 7);  R1(c, d, a, b, 10, 11); R1(b, c, d, a, 11, 19);
    R1(a, b, c, d, 12, 3); R1(d, a, b, c, 13, 7); R1(c, d, a, b, 14, 11); R1(b, c, d, a, 15, 19);

    R2(a, b, c, d, 0, 3);  R2(d, a, b, c, 4, 5);  R2(c, d, a, b, 8, 9);  R2(b, c, d, a, 12, 13);
    R2(a, b, c, d, 1, 3);  R2(d, a, b, c, 5, 5);  R2(c, d, a, b, 9, 9);  R2(b, c, d, a, 13, 13);
    R2(a, b, c, d, 2, 3);  R2(d, a, b, c, 6, 5);  R2(c, d, a, b, 10, 9); R2(b, c, d, a, 14, 13);
    R2(a, b, c, d, 3, 3);  R2(d, a, b, c, 7, 5);  R2(c, d, a, b, 11, 9); R2(b, c, d, a, 15, 13);

    R3(a, b, c, d, 0, 3);  R3(d, a, b, c, 8, 9);  R3(c, d, a, b, 4, 11); R3(b, c, d, a, 12, 15);
    R3(a, b, c, d, 2, 3);  R3(d, a, b, c, 10, 9); R3(c, d, a, b, 6, 11); R3(b, c, d, a, 14, 15);
    R3(a, b, c, d, 1, 3);  R3(d, a, b, c, 9, 9);  R3(c, d, a, b, 5, 11); R3(b, c, d, a, 13, 15);
    R3(a, b, c, d, 3, 3);  R3(d, a, b, c, 11, 9); R3(c, d, a, b, 7, 11); R3(b, c, d, a, 15, 15);

#undef F
#undef G
#undef H
#undef R1
#undef R2
#undef R3

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

static void md4_init(kwifi_md4_ctx *ctx)
{
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xEFCDAB89u;
    ctx->h[2] = 0x98BADCFEu;
    ctx->h[3] = 0x10325476u;
    ctx->bits = 0u;
    ctx->used = 0u;
}

static void md4_update(kwifi_md4_ctx *ctx, const uint8_t *data, uint32_t len)
{
    ctx->bits += (uint64_t)len * 8ull;
    while (len)
    {
        uint32_t chunk = 64u - ctx->used;
        if (chunk > len)
            chunk = len;
        kcopy(ctx->buf + ctx->used, data, chunk);
        ctx->used += chunk;
        data += chunk;
        len -= chunk;
        if (ctx->used == 64u)
        {
            md4_transform(ctx->h, ctx->buf);
            ctx->used = 0u;
        }
    }
}

static void md4_final(kwifi_md4_ctx *ctx, uint8_t out[16])
{
    uint8_t pad[64];
    uint8_t bits[8];

    kzero(pad, sizeof(pad));
    pad[0] = 0x80u;
    kstore_le64(bits, ctx->bits);
    md4_update(ctx, pad, (ctx->used < 56u) ? (56u - ctx->used) : (120u - ctx->used));
    md4_update(ctx, bits, sizeof(bits));

    for (uint32_t i = 0u; i < 4u; ++i)
    {
        out[i * 4u + 0u] = (uint8_t)(ctx->h[i] & 0xFFu);
        out[i * 4u + 1u] = (uint8_t)((ctx->h[i] >> 8) & 0xFFu);
        out[i * 4u + 2u] = (uint8_t)((ctx->h[i] >> 16) & 0xFFu);
        out[i * 4u + 3u] = (uint8_t)((ctx->h[i] >> 24) & 0xFFu);
    }
}

void kwifi_md4(const uint8_t *data, uint32_t len, uint8_t out[16])
{
    kwifi_md4_ctx ctx;
    md4_init(&ctx);
    if (data && len)
        md4_update(&ctx, data, len);
    md4_final(&ctx, out);
}

void kwifi_mschapv2_nt_password_hash(const char *password, uint8_t out[16])
{
    uint8_t unicode[256];
    uint32_t pass_len = kcstr_len_cap(password, 128u);

    for (uint32_t i = 0u; i < pass_len; ++i)
    {
        unicode[i * 2u] = (uint8_t)password[i];
        unicode[i * 2u + 1u] = 0u;
    }

    kwifi_md4(unicode, pass_len * 2u, out);
}

static const uint8_t kwifi_des_ip[64] = {
    58, 50, 42, 34, 26, 18, 10, 2,
    60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6,
    64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1,
    59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5,
    63, 55, 47, 39, 31, 23, 15, 7};

static const uint8_t kwifi_des_fp[64] = {
    40, 8, 48, 16, 56, 24, 64, 32,
    39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30,
    37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28,
    35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26,
    33, 1, 41, 9, 49, 17, 57, 25};

static const uint8_t kwifi_des_e[48] = {
    32, 1, 2, 3, 4, 5,
    4, 5, 6, 7, 8, 9,
    8, 9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32, 1};

static const uint8_t kwifi_des_p[32] = {
    16, 7, 20, 21, 29, 12, 28, 17,
    1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9,
    19, 13, 30, 6, 22, 11, 4, 25};

static const uint8_t kwifi_des_pc1[56] = {
    57, 49, 41, 33, 25, 17, 9,
    1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27,
    19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,
    7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29,
    21, 13, 5, 28, 20, 12, 4};

static const uint8_t kwifi_des_pc2[48] = {
    14, 17, 11, 24, 1, 5,
    3, 28, 15, 6, 21, 10,
    23, 19, 12, 4, 26, 8,
    16, 7, 27, 20, 13, 2,
    41, 52, 31, 37, 47, 55,
    30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53,
    46, 42, 50, 36, 29, 32};

static const uint8_t kwifi_des_shifts[16] = {
    1, 1, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 1};

static const uint8_t kwifi_des_sbox[8][64] = {
    {14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
     0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
     15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13},
    {15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
     3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
     13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9},
    {10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
     1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12},
    {7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
     13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
     3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14},
    {2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
     14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
     11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3},
    {12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
     10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
     4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13},
    {4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
     13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
     6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12},
    {13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
     1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
     2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}};

static uint64_t kwifi_des_load_be64(const uint8_t p[8])
{
    uint64_t v = 0u;

    for (uint32_t i = 0u; i < 8u; ++i)
        v = (v << 8u) | (uint64_t)p[i];
    return v;
}

static void kwifi_des_store_be64(uint8_t p[8], uint64_t v)
{
    for (uint32_t i = 0u; i < 8u; ++i)
        p[i] = (uint8_t)(v >> ((7u - i) * 8u));
}

static uint64_t kwifi_des_permute(uint64_t input, uint32_t input_bits, const uint8_t *table, uint32_t output_bits)
{
    uint64_t output = 0u;

    for (uint32_t i = 0u; i < output_bits; ++i)
    {
        output <<= 1u;
        output |= (input >> (input_bits - (uint32_t)table[i])) & 1u;
    }

    return output;
}

static uint32_t kwifi_des_rotl28(uint32_t value, uint32_t shift)
{
    value &= 0x0FFFFFFFu;
    return ((value << shift) | (value >> (28u - shift))) & 0x0FFFFFFFu;
}

static uint8_t kwifi_des_with_odd_parity(uint8_t value)
{
    uint8_t x = (uint8_t)(value & 0xFEu);

    x ^= (uint8_t)(x >> 4u);
    x ^= (uint8_t)(x >> 2u);
    x ^= (uint8_t)(x >> 1u);
    return (uint8_t)((value & 0xFEu) | ((~x) & 1u));
}

static void kwifi_des_expand_7byte_key(const uint8_t key7[7], uint8_t key8[8])
{
    key8[0] = (uint8_t)(key7[0] & 0xFEu);
    key8[1] = (uint8_t)(((key7[0] << 7u) | (key7[1] >> 1u)) & 0xFEu);
    key8[2] = (uint8_t)(((key7[1] << 6u) | (key7[2] >> 2u)) & 0xFEu);
    key8[3] = (uint8_t)(((key7[2] << 5u) | (key7[3] >> 3u)) & 0xFEu);
    key8[4] = (uint8_t)(((key7[3] << 4u) | (key7[4] >> 4u)) & 0xFEu);
    key8[5] = (uint8_t)(((key7[4] << 3u) | (key7[5] >> 5u)) & 0xFEu);
    key8[6] = (uint8_t)(((key7[5] << 2u) | (key7[6] >> 6u)) & 0xFEu);
    key8[7] = (uint8_t)((key7[6] << 1u) & 0xFEu);

    for (uint32_t i = 0u; i < 8u; ++i)
        key8[i] = kwifi_des_with_odd_parity(key8[i]);
}

static void kwifi_des_make_subkeys(const uint8_t key8[8], uint64_t subkeys[16])
{
    uint64_t key64 = kwifi_des_load_be64(key8);
    uint64_t key56 = kwifi_des_permute(key64, 64u, kwifi_des_pc1, 56u);
    uint32_t c = (uint32_t)((key56 >> 28u) & 0x0FFFFFFFu);
    uint32_t d = (uint32_t)(key56 & 0x0FFFFFFFu);

    for (uint32_t i = 0u; i < 16u; ++i)
    {
        uint64_t cd;

        c = kwifi_des_rotl28(c, kwifi_des_shifts[i]);
        d = kwifi_des_rotl28(d, kwifi_des_shifts[i]);
        cd = ((uint64_t)c << 28u) | (uint64_t)d;
        subkeys[i] = kwifi_des_permute(cd, 56u, kwifi_des_pc2, 48u);
    }
}

static uint32_t kwifi_des_f(uint32_t r, uint64_t subkey)
{
    uint64_t e = kwifi_des_permute((uint64_t)r, 32u, kwifi_des_e, 48u) ^ subkey;
    uint32_t s = 0u;

    for (uint32_t i = 0u; i < 8u; ++i)
    {
        uint8_t block = (uint8_t)((e >> ((7u - i) * 6u)) & 0x3Fu);
        uint8_t row = (uint8_t)(((block & 0x20u) >> 4u) | (block & 0x01u));
        uint8_t col = (uint8_t)((block >> 1u) & 0x0Fu);
        s = (s << 4u) | (uint32_t)kwifi_des_sbox[i][row * 16u + col];
    }

    return (uint32_t)kwifi_des_permute((uint64_t)s, 32u, kwifi_des_p, 32u);
}

static void kwifi_des_encrypt_7byte_key(const uint8_t clear[8], const uint8_t key7[7], uint8_t cipher[8])
{
    uint8_t key8[8];
    uint64_t subkeys[16];
    uint64_t block;
    uint32_t l;
    uint32_t r;

    kwifi_des_expand_7byte_key(key7, key8);
    kwifi_des_make_subkeys(key8, subkeys);

    block = kwifi_des_permute(kwifi_des_load_be64(clear), 64u, kwifi_des_ip, 64u);
    l = (uint32_t)(block >> 32u);
    r = (uint32_t)(block & 0xFFFFFFFFu);

    for (uint32_t i = 0u; i < 16u; ++i)
    {
        uint32_t old_l = l;
        l = r;
        r = old_l ^ kwifi_des_f(r, subkeys[i]);
    }

    block = ((uint64_t)r << 32u) | (uint64_t)l;
    block = kwifi_des_permute(block, 64u, kwifi_des_fp, 64u);
    kwifi_des_store_be64(cipher, block);
}

void kwifi_mschapv2_hash_nt_password_hash(const uint8_t nt_hash[16], uint8_t out[16])
{
    if (!out)
        return;
    if (!nt_hash)
    {
        kzero(out, 16u);
        return;
    }
    kwifi_md4(nt_hash, 16u, out);
}

void kwifi_mschapv2_challenge_hash(const uint8_t peer_challenge[16],
                                   const uint8_t authenticator_challenge[16],
                                   const char *username,
                                   uint8_t out[8])
{
    uint8_t digest[20];
    uint32_t username_len = kcstr_len_cap(username, 256u);
    kwifi_sha1_ctx ctx;

    if (!out)
        return;
    kzero(out, 8u);
    if (!peer_challenge || !authenticator_challenge)
        return;

    sha1_init(&ctx);
    sha1_update(&ctx, peer_challenge, 16u);
    sha1_update(&ctx, authenticator_challenge, 16u);
    if (username && username_len)
        sha1_update(&ctx, (const uint8_t *)username, username_len);
    sha1_final(&ctx, digest);
    kcopy(out, digest, 8u);
}

void kwifi_mschapv2_challenge_response(const uint8_t challenge[8],
                                       const uint8_t nt_hash[16],
                                       uint8_t out[24])
{
    uint8_t z_hash[21];

    if (!out)
        return;
    kzero(out, 24u);
    if (!challenge || !nt_hash)
        return;

    kzero(z_hash, sizeof(z_hash));
    kcopy(z_hash, nt_hash, 16u);
    kwifi_des_encrypt_7byte_key(challenge, z_hash + 0u, out + 0u);
    kwifi_des_encrypt_7byte_key(challenge, z_hash + 7u, out + 8u);
    kwifi_des_encrypt_7byte_key(challenge, z_hash + 14u, out + 16u);
}

void kwifi_mschapv2_generate_nt_response(const uint8_t authenticator_challenge[16],
                                         const uint8_t peer_challenge[16],
                                         const char *username,
                                         const char *password,
                                         uint8_t out[24])
{
    uint8_t challenge[8];
    uint8_t nt_hash[16];

    if (!out)
        return;
    kzero(out, 24u);
    if (!authenticator_challenge || !peer_challenge)
        return;

    kwifi_mschapv2_challenge_hash(peer_challenge, authenticator_challenge, username, challenge);
    kwifi_mschapv2_nt_password_hash(password, nt_hash);
    kwifi_mschapv2_challenge_response(challenge, nt_hash, out);
}

void kwifi_mschapv2_generate_authenticator_response(const char *password,
                                                    const uint8_t nt_response[24],
                                                    const uint8_t peer_challenge[16],
                                                    const uint8_t authenticator_challenge[16],
                                                    const char *username,
                                                    char out[43])
{
    static const uint8_t magic1[39] = {
        0x4Du, 0x61u, 0x67u, 0x69u, 0x63u, 0x20u, 0x73u, 0x65u, 0x72u, 0x76u,
        0x65u, 0x72u, 0x20u, 0x74u, 0x6Fu, 0x20u, 0x63u, 0x6Cu, 0x69u, 0x65u,
        0x6Eu, 0x74u, 0x20u, 0x73u, 0x69u, 0x67u, 0x6Eu, 0x69u, 0x6Eu, 0x67u,
        0x20u, 0x63u, 0x6Fu, 0x6Eu, 0x73u, 0x74u, 0x61u, 0x6Eu, 0x74u};
    static const uint8_t magic2[41] = {
        0x50u, 0x61u, 0x64u, 0x20u, 0x74u, 0x6Fu, 0x20u, 0x6Du, 0x61u, 0x6Bu,
        0x65u, 0x20u, 0x69u, 0x74u, 0x20u, 0x64u, 0x6Fu, 0x20u, 0x6Du, 0x6Fu,
        0x72u, 0x65u, 0x20u, 0x74u, 0x68u, 0x61u, 0x6Eu, 0x20u, 0x6Fu, 0x6Eu,
        0x65u, 0x20u, 0x69u, 0x74u, 0x65u, 0x72u, 0x61u, 0x74u, 0x69u, 0x6Fu,
        0x6Eu};
    static const char hex[17] = "0123456789ABCDEF";
    uint8_t nt_hash[16];
    uint8_t nt_hash_hash[16];
    uint8_t challenge[8];
    uint8_t digest[20];
    kwifi_sha1_ctx ctx;

    if (!out)
        return;
    out[0] = 0;
    if (!nt_response || !peer_challenge || !authenticator_challenge)
        return;

    kwifi_mschapv2_nt_password_hash(password, nt_hash);
    kwifi_mschapv2_hash_nt_password_hash(nt_hash, nt_hash_hash);

    sha1_init(&ctx);
    sha1_update(&ctx, nt_hash_hash, 16u);
    sha1_update(&ctx, nt_response, 24u);
    sha1_update(&ctx, magic1, sizeof(magic1));
    sha1_final(&ctx, digest);

    kwifi_mschapv2_challenge_hash(peer_challenge, authenticator_challenge, username, challenge);

    sha1_init(&ctx);
    sha1_update(&ctx, digest, sizeof(digest));
    sha1_update(&ctx, challenge, sizeof(challenge));
    sha1_update(&ctx, magic2, sizeof(magic2));
    sha1_final(&ctx, digest);

    out[0] = 'S';
    out[1] = '=';
    for (uint32_t i = 0u; i < sizeof(digest); ++i)
    {
        out[2u + i * 2u] = hex[(digest[i] >> 4u) & 0x0Fu];
        out[3u + i * 2u] = hex[digest[i] & 0x0Fu];
    }
    out[42] = 0;
}
