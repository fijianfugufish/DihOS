#pragma once
#include_next <stddef.h>
#if defined(__DIHOS__)
/* The small legacy C header omits C11 max_align_t. Use Clang's target ABI
 * definition rather than borrowing a host libc layout. */
#include <__stddef_max_align_t.h>
#endif
