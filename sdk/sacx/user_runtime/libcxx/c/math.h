#pragma once
#include_next <math.h>
/* ISO C99 declarations required by libc++ <cmath>. These declare the target
 * ABI, not a complete libm implementation: unsupported uses remain link errors.
 * FP_* values are implementation-defined classification tags. */
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#ifdef __cplusplus
extern "C" {
#endif
#define DIHOS_MATH_UNARY(name) double name(double);float name##f(float);long double name##l(long double);
#define DIHOS_MATH_BINARY(name) double name(double,double);float name##f(float,float);long double name##l(long double,long double);
DIHOS_MATH_UNARY(acos) DIHOS_MATH_UNARY(asin) DIHOS_MATH_UNARY(atan)
DIHOS_MATH_UNARY(acosh) DIHOS_MATH_UNARY(asinh) DIHOS_MATH_UNARY(atanh)
DIHOS_MATH_UNARY(cos) DIHOS_MATH_UNARY(sin) DIHOS_MATH_UNARY(tan)
DIHOS_MATH_UNARY(cosh) DIHOS_MATH_UNARY(sinh) DIHOS_MATH_UNARY(tanh)
DIHOS_MATH_UNARY(exp) DIHOS_MATH_UNARY(exp2) DIHOS_MATH_UNARY(expm1)
DIHOS_MATH_UNARY(log) DIHOS_MATH_UNARY(log2) DIHOS_MATH_UNARY(log10) DIHOS_MATH_UNARY(log1p) DIHOS_MATH_UNARY(logb)
DIHOS_MATH_UNARY(sqrt) DIHOS_MATH_UNARY(cbrt) DIHOS_MATH_UNARY(fabs)
DIHOS_MATH_UNARY(ceil) DIHOS_MATH_UNARY(floor) DIHOS_MATH_UNARY(trunc)
DIHOS_MATH_UNARY(round) DIHOS_MATH_UNARY(rint) DIHOS_MATH_UNARY(nearbyint)
DIHOS_MATH_UNARY(erf) DIHOS_MATH_UNARY(erfc) DIHOS_MATH_UNARY(tgamma) DIHOS_MATH_UNARY(lgamma)
DIHOS_MATH_BINARY(atan2) DIHOS_MATH_BINARY(pow) DIHOS_MATH_BINARY(hypot)
DIHOS_MATH_BINARY(fmod) DIHOS_MATH_BINARY(remainder) DIHOS_MATH_BINARY(copysign)
DIHOS_MATH_BINARY(fdim) DIHOS_MATH_BINARY(fmax) DIHOS_MATH_BINARY(fmin) DIHOS_MATH_BINARY(nextafter)
#define DIHOS_MATH_INT(name,type) type name(double);type name##f(float);type name##l(long double);
DIHOS_MATH_INT(ilogb,int) DIHOS_MATH_INT(lround,long) DIHOS_MATH_INT(llround,long long)
DIHOS_MATH_INT(lrint,long) DIHOS_MATH_INT(llrint,long long)
double frexp(double,int*);float frexpf(float,int*);long double frexpl(long double,int*);
double ldexp(double,int);float ldexpf(float,int);long double ldexpl(long double,int);
double scalbn(double,int);float scalbnf(float,int);long double scalbnl(long double,int);
double scalbln(double,long);float scalblnf(float,long);long double scalblnl(long double,long);
double modf(double,double*);float modff(float,float*);long double modfl(long double,long double*);
double remquo(double,double,int*);float remquof(float,float,int*);long double remquol(long double,long double,int*);
double nexttoward(double,long double);float nexttowardf(float,long double);long double nexttowardl(long double,long double);
double fma(double,double,double);float fmaf(float,float,float);long double fmal(long double,long double,long double);
double nan(const char*);float nanf(const char*);long double nanl(const char*);
#undef DIHOS_MATH_UNARY
#undef DIHOS_MATH_BINARY
#undef DIHOS_MATH_INT
#ifdef __cplusplus
}
#endif
