#pragma once

/* Declarations only for the first upstream utility slice.  Mesa's IR3 build
 * will later supply the small math implementations it actually reaches. */
double floor(double value);
double ceil(double value);
double fabs(double value);
double fmax(double left, double right);
double fmin(double left, double right);
float floorf(float value);
float ceilf(float value);
float fabsf(float value);
float sqrtf(float value);
float roundf(float value);
long lrintf(float value);

#define isfinite(value) __builtin_isfinite(value)
#define isnan(value) __builtin_isnan(value)
