#pragma once

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *dst, const void *src, size_t size);
void *memmove(void *dst, const void *src, size_t size);
void *memset(void *dst, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);
void *memchr(const void *data, int value, size_t size);
size_t strlen(const char *text);
size_t strnlen(const char *text, size_t maximum);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t size);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t size);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t size);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t size);
char *strchr(const char *text, int value);
char *strrchr(const char *text, int value);
char *strstr(const char *text, const char *needle);
char *strdup(const char *text);
char *strndup(const char *text, size_t size);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
char *strpbrk(const char *text, const char *accept);
char *strtok_r(char *text, const char *delimiters, char **save);
char *strtok(char *text, const char *delimiters);
char *strerror(int error);
#ifdef __cplusplus
}
#endif
