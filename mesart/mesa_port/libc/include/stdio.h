#pragma once

#include <stdarg.h>
#include <stddef.h>

/* Logging is intentionally optional in the first Mesart Mesa slice.  The
 * declarations let upstream utility headers compile; the runtime will later
 * route the subset it uses to a brokered diagnostic channel. */
typedef struct mesart_FILE FILE;

#define stdout ((FILE *)0)
#define stderr ((FILE *)0)

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int snprintf(char *text, size_t bytes, const char *format, ...);
int vsnprintf(char *text, size_t bytes, const char *format, va_list args);
int fflush(FILE *stream);
