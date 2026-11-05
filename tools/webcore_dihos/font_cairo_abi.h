#pragma once
/* Cairo's color-glyph renderer uses the compiler's stack-allocation extension.
 * It must never resolve to a host libc symbol on the freestanding target. */
#ifndef alloca
#define alloca(bytes) __builtin_alloca(bytes)
#endif
