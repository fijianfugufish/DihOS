#pragma once
#include <sys/types.h>
typedef long clock_t;
struct tm{int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst;};
#ifdef __cplusplus
extern "C" {
#endif
time_t time(time_t *value);
clock_t clock(void);
char *ctime(const time_t *value);
struct tm *localtime(const time_t *value);
struct tm *gmtime(const time_t *value);
time_t mktime(struct tm *value);
size_t strftime(char *dst, size_t capacity, const char *format, const struct tm *value);
#ifdef __cplusplus
}
#endif
