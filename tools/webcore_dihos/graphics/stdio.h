#pragma once
#include_next <stdio.h>
/* The native stdio archive provides vfprintf and its owned stdout stream. */
static inline int dihos_cairo_vprintf(const char* format,va_list arguments)
{ return vfprintf(stdout,format,arguments); }
#define vprintf dihos_cairo_vprintf
