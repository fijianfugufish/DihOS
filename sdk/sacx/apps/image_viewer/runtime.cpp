#include <stdint.h>

typedef __SIZE_TYPE__ editor_size_t;

extern "C" __attribute__((noinline)) void *memcpy(void *dst, const void *src, editor_size_t count)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    const volatile uint8_t *in = (const volatile uint8_t *)src;
    for (editor_size_t i = 0; i < count; ++i)
        out[i] = in[i];
    return dst;
}

extern "C" __attribute__((noinline)) void *memset(void *dst, int value, editor_size_t count)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    for (editor_size_t i = 0; i < count; ++i)
        out[i] = (uint8_t)value;
    return dst;
}

