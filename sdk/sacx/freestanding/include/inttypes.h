#pragma once
#include <stdint.h>
typedef struct{intmax_t quot,rem;}imaxdiv_t;
#define PRId8 "d"
#define PRIi8 "i"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRId16 "d"
#define PRIi16 "i"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"
#define SCNx32 "x"
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIdMAX "ld"
#define PRIuMAX "lu"
#define PRIxMAX "lx"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#define PRIxPTR "lx"
intmax_t strtoimax(const char *text,char **end,int base);
uintmax_t strtoumax(const char *text,char **end,int base);
