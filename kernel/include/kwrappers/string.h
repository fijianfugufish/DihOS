#pragma once
#include <stddef.h> // for size_t
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
int strcontains(const char *haystack, const char *needle);

typedef struct ksb
{
    char *dst;
    size_t cap;
    size_t len;
    int overflow;
} ksb;

typedef enum
{
    KSB_END = 0,
    KSB_S,
    KSB_HEX8,
    KSB_HEX32,
} ksb_tok;

void ksb_init(ksb *b, char *dst, size_t cap);
void ksb_clear(ksb *b);
void ksb_putc(ksb *b, char c);
void ksb_puts(ksb *b, const char *s);
void ksb_put_hex8(ksb *b, uint8_t v);
void ksb_put_hex32(ksb *b, uint32_t v);

// varargs formatting
void ksb_fmt(ksb *b, ...);
