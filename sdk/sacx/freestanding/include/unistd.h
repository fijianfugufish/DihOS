#pragma once
#include <sys/types.h>
#define F_OK 0
#define R_OK 4
#define W_OK 2
#ifdef __cplusplus
extern "C" {
#endif
int access(const char *path,int mode);
int unlink(const char *path);
int rmdir(const char *path);
int unlinkat(int directory,const char *path,int flags);
ssize_t pread(int fd,void *buffer,size_t count,off_t offset);
ssize_t pwrite(int fd,const void *buffer,size_t count,off_t offset);
#ifdef __cplusplus
}
#endif
