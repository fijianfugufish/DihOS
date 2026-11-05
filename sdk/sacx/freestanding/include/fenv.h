#pragma once
#include <stdint.h>
// Native AArch64 FPCR/FPSR layout; libc++ includes this through its C wrapper.
typedef struct { uint64_t fpcr,fpsr; } fenv_t;
typedef uint32_t fexcept_t;
#define FE_INVALID 1
#define FE_DIVBYZERO 2
#define FE_OVERFLOW 4
#define FE_UNDERFLOW 8
#define FE_INEXACT 16
#define FE_ALL_EXCEPT 31
#define FE_TONEAREST 0
#define FE_UPWARD 0x400000
#define FE_DOWNWARD 0x800000
#define FE_TOWARDZERO 0xc00000
#define FE_DFL_ENV ((const fenv_t*)-1)
#ifdef __cplusplus
extern "C" {
#endif
int fegetround(void);
int fesetround(int);
int fegetenv(fenv_t*);
int fesetenv(const fenv_t*);
int feholdexcept(fenv_t*);
int feupdateenv(const fenv_t*);
int feclearexcept(int);
int fetestexcept(int);
int fegetexceptflag(fexcept_t*,int);
int fesetexceptflag(const fexcept_t*,int);
int feraiseexcept(int);
#ifdef __cplusplus
}
#endif
