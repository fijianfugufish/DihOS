#pragma once

/* Mesart bundles are freestanding.  Keep assertion failures local to the
 * untrusted EL0 renderer instead of borrowing a host C runtime. */
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression)                                                     \
    ((expression) ? (void)0 : __builtin_trap())
#endif
