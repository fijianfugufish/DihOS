#pragma once
// Native EL0 C ABI. Search after libc++ wrappers and before legacy C headers.
#include_next <errno.h>
#ifdef __cplusplus
extern "C" {
#endif
int *__errno_location(void) __attribute__((const));
#ifdef __cplusplus
}
#endif
#undef errno
#define errno (*__errno_location())
