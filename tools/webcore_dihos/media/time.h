#pragma once
#include_next <time.h>
#if defined(__DIHOS__)
#ifdef __cplusplus
extern "C" {
#endif
size_t dihos_media_strftime(char*,size_t,const char*,const struct tm*);
#ifdef __cplusplus
}
#endif
#define strftime dihos_media_strftime
#endif
