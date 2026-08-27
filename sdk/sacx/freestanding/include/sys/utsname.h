#pragma once
struct utsname { char sysname[32],nodename[32],release[32],version[32],machine[32]; };
#ifdef __cplusplus
extern "C" {
#endif
int uname(struct utsname *result);
#ifdef __cplusplus
}
#endif
