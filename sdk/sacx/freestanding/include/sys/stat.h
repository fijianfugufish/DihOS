#pragma once
#include <sys/types.h>
struct stat { mode_t st_mode; off_t st_size; time_t st_mtime; };
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_IRWXU 0700
#ifdef __cplusplus
extern "C" {
#endif
int stat(const char *path,struct stat *result);
int mkdir(const char *path,mode_t mode);
int fstatat(int directory,const char *path,struct stat *result,int flags);
#ifdef __cplusplus
}
#endif
