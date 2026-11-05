#pragma once
#include <locale.h>
/* ICU's private locale-environment parser uses these category tags even on
 * targets without POSIX services. Keep LC_ALL=0 from the existing C ABI.
 * These declarations provide no setlocale/getenv implementation or capability. */
#ifndef LC_CTYPE
#define LC_CTYPE 2
#endif
#ifndef LC_MESSAGES
#define LC_MESSAGES 6
#endif
/* putil.cpp chooses its own parser; don't override its source-local macro. */
#undef U_POSIX_LOCALE
