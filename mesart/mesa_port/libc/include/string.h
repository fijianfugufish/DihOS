#pragma once

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t bytes);
void *memmove(void *destination, const void *source, size_t bytes);
void *memset(void *destination, int value, size_t bytes);
int memcmp(const void *left, const void *right, size_t bytes);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t bytes);
char *strdup(const char *text);
char *strtok_r(char *text, const char *delimiters, char **save);
