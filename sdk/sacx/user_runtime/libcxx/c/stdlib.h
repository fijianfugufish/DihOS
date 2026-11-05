#pragma once
#include_next <stdlib.h>
/* ISO C declarations needed to parse libc++'s headers. Unimplemented entries
 * remain link errors; they are not success-returning service stubs. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {int quot,rem;} div_t;
typedef struct {long quot,rem;} ldiv_t;
typedef struct {long long quot,rem;} lldiv_t;
long labs(long);
long long llabs(long long);
div_t div(int,int);
ldiv_t ldiv(long,long);
lldiv_t lldiv(long long,long long);
double atof(const char*);
long atol(const char*);
long long atoll(const char*);
long double strtold(const char*,char**);
void *aligned_alloc(size_t,size_t);
void _Exit(int);
#ifdef __cplusplus
}
#endif
