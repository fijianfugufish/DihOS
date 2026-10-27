#pragma once
#include <stdint.h>
#if defined(__aarch64__) || (defined(__x86_64__) && defined(DIHSCOVER_FREESTANDING))
typedef uint64_t jmp_buf[24];
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((returns_twice)) int setjmp(jmp_buf);
__attribute__((noreturn)) void longjmp(jmp_buf,int);
#ifdef __cplusplus
}
#endif
#else
#include_next <setjmp.h>
#endif
