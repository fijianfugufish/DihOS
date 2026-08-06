#pragma once

#if defined(NDEBUG)
#define assert(expression) ((void)0)
#else
#define assert(expression) ((void)(expression))
#endif

