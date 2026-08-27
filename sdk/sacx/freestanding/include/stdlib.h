#pragma once

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
int abs(int value);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
long long strtoll(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
double strtod(const char *text, char **end);
float strtof(const char *text, char **end);
int atoi(const char *text);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void abort(void);
void exit(int status);
int atexit(void (*function)(void));
int rand(void);
void srand(unsigned int seed);
char *getenv(const char *name);
char *realpath(const char *path,char *resolved);
#ifdef __cplusplus
}
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
