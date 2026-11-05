#pragma once
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void* malloc(size_t);
void* calloc(size_t,size_t);
void* realloc(void*,size_t);
void free(void*);
long strtol(const char*,char**,int);
static inline int abs(int x){return x<0?-x:x;}
