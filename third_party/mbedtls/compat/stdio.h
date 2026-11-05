#pragma once
#include <stddef.h>
#include <stdarg.h>
/* File I/O and self tests are disabled in the native TLS profile. */
typedef struct dihos_tls_no_files FILE;
#define EOF (-1)
