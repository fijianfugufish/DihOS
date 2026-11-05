#pragma once
/* Private AAPCS64 jump-buffer ABI for the pinned musl ARM64 setjmp/longjmp
 * assembly. No signal-mask or POSIX sigsetjmp service is exposed. */
#if !defined(__aarch64__) || __SIZEOF_LONG__ != 8
#error Revalidate the native ARM64 jump-buffer ABI
#endif
typedef unsigned long jmp_buf[22];
#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf) __attribute__((returns_twice));
void longjmp(jmp_buf,int) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
