#ifndef DIHOS_BEARSSL_STRING_H
#define DIHOS_BEARSSL_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t len);
void *memmove(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);
int memcmp(const void *a, const void *b, size_t len);
size_t strlen(const char *text);

#endif
