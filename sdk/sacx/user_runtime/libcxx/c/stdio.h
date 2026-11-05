#pragma once
// Native EL0 stdio ABI, searched after libc++'s wrapper. FILE stays private.
// Legacy SACX's dihs_file shim continues to use the freestanding header.
#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _IO_FILE FILE;
extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;
int printf(const char*,...);
int fprintf(FILE*,const char*,...);
int vfprintf(FILE*,const char*,va_list);
int snprintf(char*,size_t,const char*,...);
int vsnprintf(char*,size_t,const char*,va_list);
int sprintf(char*,const char*,...);
int sscanf(const char*,const char*,...);
int vsscanf(const char*,const char*,va_list);
FILE *fopen(const char*,const char*);
int fclose(FILE*);
int fflush(FILE*);
char *fgets(char*,int,FILE*);
int fputs(const char*,FILE*);
int fputc(int,FILE*);
size_t fread(void*,size_t,size_t,FILE*);
size_t fwrite(const void*,size_t,size_t,FILE*);
int fseek(FILE*,long,int);
long ftell(FILE*);
int feof(FILE*);
int ferror(FILE*);
void clearerr(FILE*);
void setbuf(FILE*,char*);
int setvbuf(FILE*,char*,int,size_t);
int remove(const char*);
int rename(const char*,const char*);
#ifdef __cplusplus
}
#endif
#define EOF (-1)
#define BUFSIZ 1024
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
