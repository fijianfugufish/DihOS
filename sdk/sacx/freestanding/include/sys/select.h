#pragma once
#include <sys/time.h>
typedef struct{unsigned long bits[16];}fd_set;
#define FD_ZERO(set) ((void)0)
#define FD_SET(fd,set) ((void)0)
#define FD_ISSET(fd,set) (0)

