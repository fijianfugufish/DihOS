#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
long strtol(const char *nptr, char **endptr, int base);

static inline int abs(int x)
{
    return (x < 0) ? -x : x;
}

#ifdef __cplusplus
}
#endif
