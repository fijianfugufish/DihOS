#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
/* libavutil always builds its optional file helpers. This decoder receives
 * checked memory through AVIO; it has no descriptor namespace or file grant.
 * Deny the helpers rather than exposing host/guest filesystem calls. */
static inline int open(const char* path,int flags,...)
{ (void)path;(void)flags;errno=EACCES;return -1; }
static inline int close(int fd)
{ (void)fd;errno=EBADF;return -1; }
static inline ptrdiff_t read(int fd,void* data,size_t count)
{ (void)fd;(void)data;(void)count;errno=EBADF;return -1; }
static inline int fstat(int fd,struct stat* result)
{ (void)fd;(void)result;errno=EBADF;return -1; }
static inline FILE* fdopen(int fd,const char* mode)
{ (void)fd;(void)mode;errno=EBADF;return NULL; }
#ifndef O_APPEND
#define O_APPEND 1024
#endif
