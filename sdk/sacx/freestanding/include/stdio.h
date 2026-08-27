#pragma once

#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct dihs_file FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int snprintf(char *dst, size_t capacity, const char *format, ...);
int vsnprintf(char *dst, size_t capacity, const char *format, va_list args);
int sscanf(const char *text, const char *format, ...);
int sprintf(char *dst,const char *format,...);
int fprintf(FILE *stream,const char *format,...);
int vfprintf(FILE *stream,const char *format,va_list args);
FILE *fopen(const char *path,const char *mode);
int fclose(FILE *stream);
char *fgets(char *dst,int capacity,FILE *stream);
int fputs(const char *text,FILE *stream);
int fputc(int value,FILE *stream);
size_t fread(void *dst,size_t size,size_t count,FILE *stream);
size_t fwrite(const void *src,size_t size,size_t count,FILE *stream);
int fseek(FILE *stream,long offset,int origin);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fflush(FILE *stream);
void setbuf(FILE *stream,char *buffer);
int remove(const char *path);
int rename(const char *old_path,const char *new_path);
#ifdef __cplusplus
}
#endif

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
