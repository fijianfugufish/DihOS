#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include "../graphics/cairo_mutex_dihos.h"
/* The native profile has no directory/cache/file capability. These private
 * adapters always report denial; matching registered memory faces is real. */
int dihos_fc_stat(const char*,struct stat*);
int dihos_fc_fstat(int,struct stat*);
int dihos_fc_open(const char*,int,...);
int dihos_fc_close(int);
ssize_t dihos_fc_read(int,void*,size_t);
ssize_t dihos_fc_write(int,const void*,size_t);
off_t dihos_fc_lseek(int,off_t,int);
int dihos_fc_access(const char*,int);
int dihos_fc_path(const char*);
int dihos_fc_mkdir(const char*,mode_t);
int dihos_fc_link(const char*,const char*);
int dihos_fc_fcntl(int,int,...);
DIR* dihos_fc_opendir(const char*);
struct dirent* dihos_fc_readdir(DIR*);
int dihos_fc_closedir(DIR*);
char* dihos_fc_getcwd(char*,size_t);
char* dihos_fc_realpath(const char*,char*);
int dihos_fc_getpid(void);
unsigned dihos_fc_io_denials(void);
int dihos_fc_utimes(const char*,const struct timeval*);
void dihos_fc_perror(const char*);
int32_t dihos_fc_random(void);
FILE* dihos_fc_fdopen(int,const char*);
#define fdopen dihos_fc_fdopen
#define utimes dihos_fc_utimes
#define chmod dihos_fc_mkdir
#define perror dihos_fc_perror
#define timercmp(a,b,op) (((a)->tv_sec==(b)->tv_sec)?((a)->tv_usec op (b)->tv_usec):((a)->tv_sec op (b)->tv_sec))
#define stat(...) dihos_fc_stat(__VA_ARGS__)
#define lstat(...) dihos_fc_stat(__VA_ARGS__)
#define fstat dihos_fc_fstat
#define open dihos_fc_open
#define close dihos_fc_close
#define read dihos_fc_read
#define write dihos_fc_write
#define lseek dihos_fc_lseek
#define access dihos_fc_access
#define unlink dihos_fc_path
#define rmdir dihos_fc_path
#define mkdir dihos_fc_mkdir
#define link dihos_fc_link
#define symlink dihos_fc_link
#define rename dihos_fc_link
#define fcntl dihos_fc_fcntl
#define opendir dihos_fc_opendir
#define readdir dihos_fc_readdir
#define closedir dihos_fc_closedir
#define getcwd dihos_fc_getcwd
#define realpath dihos_fc_realpath
#define getpid dihos_fc_getpid
typedef dihos_cairo_mutex fc_mutex_impl_t;
#define FC_MUTEX_IMPL_INIT {0,0,0}
#define fc_mutex_impl_init(m) dihos_cairo_mutex_init(m)
#define fc_mutex_impl_lock(m) dihos_cairo_mutex_lock(m)
#define fc_mutex_impl_unlock(m) dihos_cairo_mutex_unlock(m)
#define fc_mutex_impl_finish(m) dihos_cairo_mutex_fini(m)
