#pragma once
#include <sys/types.h>
struct timeval{time_t tv_sec;long tv_usec;};
#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval *time,void *zone);
#ifdef __cplusplus
}
#endif
