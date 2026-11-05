#pragma once
/* The minimal SACX C stddef header predates wchar_t. Clang's stdatomic.h
 * needs that type even though this port performs no wide-character I/O. */
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sacx_user_syscall.h>
/* File-descriptor BIOs remain present in libcrypto even with no-stdio.
 * No descriptor namespace is granted to this native crypto profile. */
static inline int dihos_ossl_close(int fd){(void)fd;errno=13;return -1;}
static inline ptrdiff_t dihos_ossl_read(int fd,void* p,size_t n){(void)fd;(void)p;(void)n;errno=13;return -1;}
static inline ptrdiff_t dihos_ossl_write(int fd,const void* p,size_t n){(void)fd;(void)p;(void)n;errno=13;return -1;}
static inline off_t dihos_ossl_lseek(int fd,off_t offset,int whence){(void)fd;(void)offset;(void)whence;errno=13;return -1;}
#define close dihos_ossl_close
#define read dihos_ossl_read
#define write dihos_ossl_write
#define lseek dihos_ossl_lseek
static inline int dihos_ossl_getpid(void){int64_t id=sacx_user_call(SACX_USER_PROCESS_SELF,0,0,0);if(id<=0 || id>0x7fffffff){errno=5;return -1;}return (int)id;}
#define getpid dihos_ossl_getpid
static inline int dihos_ossl_stat(const char* path,struct stat* output){(void)path;(void)output;errno=13;return -1;}
#define stat(path,output) dihos_ossl_stat(path,output)
/* Fixed C locale, matching this freestanding runtime's locale contract. */
static inline int dihos_ossl_isspace(int c){return c==' ' || (c>='\t' && c<='\r');}
static inline int dihos_ossl_isdigit(int c){return c>='0' && c<='9';}
static inline size_t dihos_ossl_strspn(const char* s,const char* accept){size_t n=0;for(;s[n];++n){const char* p=accept;while(*p && *p!=s[n])++p;if(!*p)break;}return n;}
static inline size_t dihos_ossl_strcspn(const char* s,const char* reject){size_t n=0;for(;s[n];++n){const char* p=reject;while(*p && *p!=s[n])++p;if(*p)break;}return n;}
#define isspace dihos_ossl_isspace
#define isdigit dihos_ossl_isdigit
#define strspn dihos_ossl_strspn
#define strcspn dihos_ossl_strcspn
