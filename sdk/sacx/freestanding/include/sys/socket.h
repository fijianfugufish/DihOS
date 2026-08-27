#pragma once
#include <sys/types.h>
typedef unsigned short sa_family_t;
typedef unsigned int socklen_t;
struct sockaddr{sa_family_t sa_family;char sa_data[14];};
#define AF_INET 2
#define AF_INET6 10

