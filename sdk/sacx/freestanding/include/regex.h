#pragma once
#include <stddef.h>
typedef struct{void *opaque;}regex_t;typedef struct{size_t rm_so,rm_eo;}regmatch_t;
#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NOSUB 4
#define REG_NOMATCH 1
#ifdef __cplusplus
extern "C" {
#endif
int regcomp(regex_t *regex,const char *pattern,int flags);
int regexec(const regex_t *regex,const char *text,size_t count,regmatch_t matches[],int flags);
void regfree(regex_t *regex);
size_t regerror(int error,const regex_t *regex,char *buffer,size_t capacity);
#ifdef __cplusplus
}
#endif
