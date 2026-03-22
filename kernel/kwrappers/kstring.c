#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "kwrappers/string.h"

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dest;
    if (d < s)
    {
        for (size_t i = 0; i < n; ++i)
            d[i] = s[i];
    }
    else
    {
        for (size_t i = n; i-- > 0;)
            d[i] = s[i];
    }
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; ++i)
        p[i] = v;
    return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i)
    {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; ++i)
        if (p[i] == v)
            return (void *)(p + i);
    return NULL;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc)
            return (int)ac - (int)bc;
        if (ac == 0)
            return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
    {
    }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; ++i)
        dst[i] = src[i];
    for (; i < n; ++i)
        dst[i] = 0;
    return dst;
}

char *strchr(const char *s, int c)
{
    char ch = (char)c;
    for (; *s; ++s)
        if (*s == ch)
            return (char *)s;
    return (ch == 0) ? (char *)s : NULL;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t i, j;

    if (!haystack || !needle)
        return NULL;

    if (*needle == 0)
        return (char *)haystack;

    for (i = 0; haystack[i]; ++i)
    {
        for (j = 0; needle[j]; ++j)
        {
            if (haystack[i + j] != needle[j])
                break;
        }

        if (needle[j] == 0)
            return (char *)(haystack + i);
    }

    return NULL;
}

int strcontains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

static const char k_hex_digits[] = "0123456789ABCDEF";

static void ksb_terminate(ksb *b)
{
    if (!b->dst || b->cap == 0)
        return;
    size_t i = (b->len < (b->cap - 1)) ? b->len : (b->cap - 1);
    b->dst[i] = 0;
}

void ksb_init(ksb *b, char *dst, size_t cap)
{
    b->dst = dst;
    b->cap = cap;
    b->len = 0;
    b->overflow = 0;
    ksb_terminate(b);
}

void ksb_clear(ksb *b)
{
    b->len = 0;
    b->overflow = 0;
    ksb_terminate(b);
}

void ksb_putc(ksb *b, char c)
{
    if (!b->dst || b->cap == 0)
        return;
    if (b->len + 1 >= b->cap)
    {
        b->overflow = 1;
        ksb_terminate(b);
        return;
    }
    b->dst[b->len++] = c;
    b->dst[b->len] = 0;
}

void ksb_puts(ksb *b, const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        ksb_putc(b, *s++);
}

void ksb_put_hex8(ksb *b, uint8_t v)
{
    ksb_putc(b, k_hex_digits[(v >> 4) & 0xF]);
    ksb_putc(b, k_hex_digits[v & 0xF]);
}

void ksb_put_hex32(ksb *b, uint32_t v)
{
    ksb_putc(b, k_hex_digits[(v >> 28) & 0xF]);
    ksb_putc(b, k_hex_digits[(v >> 24) & 0xF]);
    ksb_putc(b, k_hex_digits[(v >> 20) & 0xF]);
    ksb_putc(b, k_hex_digits[(v >> 16) & 0xF]);
    ksb_putc(b, k_hex_digits[(v >> 12) & 0xF]);
    ksb_putc(b, k_hex_digits[(v >> 8) & 0xF]);
    ksb_putc(b, k_hex_digits[(v >> 4) & 0xF]);
    ksb_putc(b, k_hex_digits[v & 0xF]);
}

void ksb_fmt(ksb *b, ...)
{
    va_list ap;
    va_start(ap, b);

    for (;;)
    {
        int tok = va_arg(ap, int);
        if (tok == KSB_END)
            break;

        if (tok == KSB_S)
        {
            const char *s = va_arg(ap, const char *);
            ksb_puts(b, s);
        }
        else if (tok == KSB_HEX8)
        {
            // promoted to int in varargs
            uint32_t v = (uint32_t)va_arg(ap, int);
            ksb_put_hex8(b, (uint8_t)v);
        }
        else if (tok == KSB_HEX32)
        {
            uint32_t v = va_arg(ap, uint32_t);
            ksb_put_hex32(b, v);
        }
        else
        {
            // unknown token -> stop so you notice
            ksb_puts(b, "<fmt?>");
            break;
        }
    }

    va_end(ap);
}