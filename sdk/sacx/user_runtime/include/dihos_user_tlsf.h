#pragma once
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
void *memcpy(void *out,const void *in,size_t bytes);
int dihos_user_allocator_printf(const char *format,...);
__attribute__((noreturn)) void dihos_user_allocator_panic(const char *reason);
#define printf dihos_user_allocator_printf
#define tlsf_assert(condition) ((condition)?(void)0:dihos_user_allocator_panic("TLSF integrity check failed"))
