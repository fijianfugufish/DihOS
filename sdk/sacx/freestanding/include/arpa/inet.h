#pragma once
#include <netinet/in.h>
#ifdef __cplusplus
extern "C" {
#endif
int inet_aton(const char *text,struct in_addr *address);
int inet_pton(int family,const char *text,void *address);
#ifdef __cplusplus
}
#endif
