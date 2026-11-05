#include "native.h"
#include <errno.h>
#include <sacx_user_syscall.h>
#include <sacx_user_random.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
static unsigned denials;
static int denied(void){__atomic_fetch_add(&denials,1,__ATOMIC_RELAXED);errno=13;return -1;}
unsigned dihos_fc_io_denials(void){return __atomic_load_n(&denials,__ATOMIC_RELAXED);}
int dihos_fc_stat(const char* p,struct stat* s){(void)p;(void)s;return denied();}
int dihos_fc_fstat(int fd,struct stat* s){(void)fd;(void)s;return denied();}
int dihos_fc_open(const char* p,int flags,...){(void)p;(void)flags;return denied();}
int dihos_fc_close(int fd){(void)fd;return denied();}
ssize_t dihos_fc_read(int fd,void* p,size_t n){(void)fd;(void)p;(void)n;return denied();}
ssize_t dihos_fc_write(int fd,const void* p,size_t n){(void)fd;(void)p;(void)n;return denied();}
off_t dihos_fc_lseek(int fd,off_t offset,int whence){(void)fd;(void)offset;(void)whence;return denied();}
int dihos_fc_access(const char* p,int mode){(void)p;(void)mode;return denied();}
int dihos_fc_path(const char* p){(void)p;return denied();}
int dihos_fc_mkdir(const char* p,mode_t mode){(void)p;(void)mode;return denied();}
int dihos_fc_link(const char* a,const char* b){(void)a;(void)b;return denied();}
int dihos_fc_fcntl(int fd,int cmd,...){(void)fd;(void)cmd;return denied();}
DIR* dihos_fc_opendir(const char* p){(void)p;denied();return NULL;}
struct dirent* dihos_fc_readdir(DIR* p){(void)p;denied();return NULL;}
int dihos_fc_closedir(DIR* p){(void)p;return denied();}
char* dihos_fc_getcwd(char* p,size_t n){(void)p;(void)n;denied();return NULL;}
char* dihos_fc_realpath(const char* p,char* out){(void)p;(void)out;denied();return NULL;}
int dihos_fc_getpid(void){long id=sacx_user_call(SACX_USER_PROCESS_SELF,0,0,0);if(id<=0 || id>0x7fffffff)__builtin_trap();return (int)id;}
int dihos_fc_utimes(const char* p,const struct timeval* t){(void)p;(void)t;return denied();}
void dihos_fc_perror(const char* prefix){fprintf(stderr,"%s: native font I/O denied (error %d)\n",prefix?prefix:"Fontconfig",errno);}
int32_t dihos_fc_random(void){uint32_t value;if(sacx_user_random(&value,sizeof(value)))__builtin_trap();return (int32_t)(value&0x7fffffffu);}
FILE* dihos_fc_fdopen(int fd,const char* mode){(void)fd;(void)mode;denied();return NULL;}
/* The native profile has the C locale. These are real C operations required by
 * face metadata, XML encoding names and Fontconfig variation formatting. */
int isspace(int c){return c==' ' || (c>='\t' && c<='\r');}
int toupper(int c){return c>='a' && c<='z'?c-('a'-'A'):c;}
int sprintf(char* destination,const char* format,...){
    va_list arguments;va_start(arguments,format);
    int result=vsnprintf(destination,INT_MAX,format,arguments);va_end(arguments);return result;
}
int fputc(int c,FILE* stream){unsigned char byte=(unsigned char)c;return fwrite(&byte,1,1,stream)==1?byte:EOF;}
void* bsearch(const void* key,const void* base,size_t count,size_t width,int (*compare)(const void*,const void*)){
    if(!width || count>SIZE_MAX/width)return NULL;
    const unsigned char* start=base;
    while(count){size_t middle=count/2;const void* item=start+middle*width;int order=compare(key,item);
        if(!order)return (void*)item;
        if(order<0)count=middle;else{start+=(middle+1)*width;count-=middle+1;}}
    return NULL;
}
