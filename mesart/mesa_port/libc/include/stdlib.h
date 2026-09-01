#pragma once

#include <stddef.h>
#include <stdint.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void abort(void) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
void *malloc(size_t bytes);
void *calloc(size_t count, size_t bytes);
void *realloc(void *memory, size_t bytes);
void free(void *memory);
char *getenv(const char *name);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
int atoi(const char *text);
