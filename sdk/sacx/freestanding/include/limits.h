#pragma once

#define CHAR_BIT 8
#define INT_MAX 2147483647
#define INT_MIN (-2147483647 - 1)
#define PATH_MAX 1024
#define UINT_MAX 4294967295u
#define LONG_MAX __LONG_MAX__
#define ULONG_MAX (__LONG_MAX__ * 2ul + 1ul)

#ifndef SCHAR_MAX
#define SCHAR_MAX __SCHAR_MAX__
#define SCHAR_MIN (-SCHAR_MAX-1)
#define UCHAR_MAX (SCHAR_MAX*2U+1U)
#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#endif
#define SHRT_MAX __SHRT_MAX__
#define SHRT_MIN (-SHRT_MAX-1)
#define USHRT_MAX (SHRT_MAX*2U+1U)
#define LONG_MIN (-LONG_MAX-1L)
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-LLONG_MAX-1LL)
#define ULLONG_MAX (LLONG_MAX*2ULL+1ULL)
#endif
