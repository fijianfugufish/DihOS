#include "browser.h"

extern "C" __attribute__((noinline)) void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n)
{
    return b_memcpy(dst, src, n);
}

extern "C" __attribute__((noinline)) void *memset(void *dst, int value, __SIZE_TYPE__ n)
{
    return b_memset(dst, value, n);
}

extern "C" __attribute__((noinline)) __SIZE_TYPE__ strlen(const char *s)
{
    return (__SIZE_TYPE__)b_strlen(s);
}

#if defined(DIHSCOVER_FREESTANDING)
extern "C" int printf(const char *format, ...)
{
    (void)format;
    return 0;
}
#endif

void *b_memcpy(void *dst, const void *src, __SIZE_TYPE__ n)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (__SIZE_TYPE__ i = 0; i < n; ++i)
        d[i] = s[i];
    return dst;
}

void *b_memset(void *dst, int value, __SIZE_TYPE__ n)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    for (__SIZE_TYPE__ i = 0; i < n; ++i)
        d[i] = (uint8_t)value;
    return dst;
}

uint32_t b_strlen(const char *s)
{
    uint32_t n = 0u;
    if (s)
        while (s[n]) ++n;
    return n;
}

int b_streq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

int b_starts(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    while (*prefix && *s == *prefix) { ++s; ++prefix; }
    return *prefix == 0;
}

void b_copy_n(char *dst, uint32_t cap, const char *src, uint32_t n)
{
    uint32_t i = 0u;
    if (!dst || !cap) return;
    if (src) while (i < n && i + 1u < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

void b_copy(char *dst, uint32_t cap, const char *src)
{
    b_copy_n(dst, cap, src, src ? b_strlen(src) : 0u);
}
