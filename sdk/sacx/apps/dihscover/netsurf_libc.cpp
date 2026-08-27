#include "dihscover.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <signal.h>
#include <dirent.h>
#include <zlib.h>
#include <iconv.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>

extern "C" const sacx_api *dihscover_netsurf_api(void);
extern "C" int errno=0;
struct dihs_file{int unused;};static dihs_file g_stdin,g_stdout,g_stderr;
extern "C" FILE *stdin=&g_stdin;extern "C" FILE *stdout=&g_stdout;extern "C" FILE *stderr=&g_stderr;

extern "C" void *memmove(void *dst,const void *src,size_t size){uint8_t*d=(uint8_t*)dst;const uint8_t*s=(const uint8_t*)src;if(d<s)for(size_t i=0;i<size;++i)d[i]=s[i];else for(size_t i=size;i;i--)d[i-1]=s[i-1];return dst;}
extern "C" int memcmp(const void*a,const void*b,size_t size){const uint8_t*x=(const uint8_t*)a,*y=(const uint8_t*)b;for(size_t i=0;i<size;++i)if(x[i]!=y[i])return x[i]<y[i]?-1:1;return 0;}
extern "C" void *memchr(const void*data,int value,size_t size){const uint8_t*p=(const uint8_t*)data;for(size_t i=0;i<size;++i)if(p[i]==(uint8_t)value)return(void*)(p+i);return 0;}
extern "C" size_t strnlen(const char*s,size_t maximum){size_t n=0;while(n<maximum&&s[n])++n;return n;}
extern "C" int strcmp(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return(unsigned char)*a-(unsigned char)*b;}
extern "C" int strncmp(const char*a,const char*b,size_t n){while(n&&*a&&*a==*b){++a;++b;--n;}return n?(unsigned char)*a-(unsigned char)*b:0;}
extern "C" char *strcpy(char*d,const char*s){char*r=d;while((*d++=*s++));return r;}
extern "C" char *strncpy(char*d,const char*s,size_t n){char*r=d;size_t i=0;for(;i<n&&s[i];++i)d[i]=s[i];for(;i<n;++i)d[i]=0;return r;}
extern "C" char *strcat(char*d,const char*s){strcpy(d+strlen(d),s);return d;}
extern "C" char *strncat(char*d,const char*s,size_t n){size_t at=strlen(d),i=0;while(i<n&&s[i])d[at++]=s[i++];d[at]=0;return d;}
extern "C" char *strchr(const char*s,int c){do{if(*s==(char)c)return(char*)s;}while(*s++);return 0;}
extern "C" char *strrchr(const char*s,int c){const char*r=0;do{if(*s==(char)c)r=s;}while(*s++);return(char*)r;}
extern "C" char *strstr(const char*s,const char*n){if(!*n)return(char*)s;size_t z=strlen(n);for(;*s;++s)if(!strncmp(s,n,z))return(char*)s;return 0;}
extern "C" char *strdup(const char*s){size_t n=strlen(s)+1;char*r=(char*)malloc(n);if(r)memcpy(r,s,n);return r;}
static int in_set(char c,const char*set){for(;*set;++set)if(c==*set)return 1;return 0;}
extern "C" size_t strspn(const char*s,const char*a){size_t n=0;while(s[n]&&in_set(s[n],a))++n;return n;}
extern "C" size_t strcspn(const char*s,const char*r){size_t n=0;while(s[n]&&!in_set(s[n],r))++n;return n;}
extern "C" char *strpbrk(const char*s,const char*a){for(;*s;++s)if(in_set(*s,a))return(char*)s;return 0;}
extern "C" char *strtok_r(char*s,const char*d,char**save){if(!s)s=*save;s+=strspn(s,d);if(!*s){*save=s;return 0;}char*end=s+strcspn(s,d);if(*end)*end++=0;*save=end;return s;}
extern "C" char *strtok(char*s,const char*d){static char*save;return strtok_r(s,d,&save);}
extern "C" char *strerror(int error){(void)error;static char text[]="operation unavailable";return text;}

extern "C" int tolower(int c){return c>='A'&&c<='Z'?c+32:c;}extern "C" int toupper(int c){return c>='a'&&c<='z'?c-32:c;}
extern "C" int isdigit(int c){return c>='0'&&c<='9';}extern "C" int isxdigit(int c){return isdigit(c)||(tolower(c)>='a'&&tolower(c)<='f');}
extern "C" int islower(int c){return c>='a'&&c<='z';}extern "C" int isupper(int c){return c>='A'&&c<='Z';}extern "C" int isalpha(int c){return islower(c)||isupper(c);}
extern "C" int isalnum(int c){return isalpha(c)||isdigit(c);}extern "C" int isspace(int c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';}
extern "C" int isblank(int c){return c==' '||c=='\t';}extern "C" int isascii(int c){return(unsigned)c<128u;}extern "C" int iscntrl(int c){return(unsigned)c<32u||c==127;}
extern "C" int isprint(int c){return c>=32&&c<127;}extern "C" int isgraph(int c){return c>32&&c<127;}extern "C" int ispunct(int c){return isgraph(c)&&!isalnum(c);}
extern "C" int strcasecmp(const char*a,const char*b){while(*a&&tolower(*a)==tolower(*b)){++a;++b;}return tolower((unsigned char)*a)-tolower((unsigned char)*b);}
extern "C" int strncasecmp(const char*a,const char*b,size_t n){while(n&&*a&&tolower(*a)==tolower(*b)){++a;++b;--n;}return n?tolower((unsigned char)*a)-tolower((unsigned char)*b):0;}

extern "C" long strtol(const char*s,char**end,int base){while(isspace(*s))++s;int sign=1;if(*s=='-'||*s=='+'){if(*s++=='-')sign=-1;}if(!base)base=(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))?16:10;if(base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;unsigned long value=0;for(;;++s){int d=isdigit(*s)?*s-'0':tolower(*s)>='a'&&tolower(*s)<='z'?tolower(*s)-'a'+10:-1;if(d<0||d>=base)break;value=value*(unsigned)base+(unsigned)d;}if(end)*end=(char*)s;return(long)value*sign;}
extern "C" unsigned long strtoul(const char*s,char**end,int base){return(unsigned long)strtol(s,end,base);}extern "C" int atoi(const char*s){return(int)strtol(s,0,10);}extern "C" int abs(int x){return x<0?-x:x;}
extern "C" long long strtoll(const char*s,char**end,int base){return(long long)strtol(s,end,base);}extern "C" unsigned long long strtoull(const char*s,char**end,int base){return(unsigned long long)strtoul(s,end,base);}
extern "C" double strtod(const char*s,char**end){while(isspace(*s))++s;double sign=1;if(*s=='-'||*s=='+'){if(*s++=='-')sign=-1;}double value=0;while(isdigit(*s))value=value*10+(*s++-'0');if(*s=='.'){double place=.1;++s;while(isdigit(*s)){value+=(*s++-'0')*place;place*=.1;}}if(*s=='e'||*s=='E'){++s;int esign=1;if(*s=='-'||*s=='+'){if(*s++=='-')esign=-1;}int exponent=0;while(isdigit(*s))exponent=exponent*10+(*s++-'0');double scale=1;while(exponent--)scale*=10;value=esign<0?value/scale:value*scale;}if(end)*end=(char*)s;return sign*value;}
extern "C" float strtof(const char*s,char**end){return(float)strtod(s,end);}
static unsigned int rand_state=1;extern "C" void srand(unsigned int seed){rand_state=seed?seed:1;}extern "C" int rand(void){rand_state=rand_state*1103515245u+12345u;return(int)(rand_state&0x7fffffffu);}
extern "C" char *getenv(const char*name){(void)name;return 0;}extern "C" char *realpath(const char*path,char*resolved){if(!path)return 0;if(!resolved)return strdup(path);strcpy(resolved,path);return resolved;}
extern "C" void qsort(void*base,size_t count,size_t size,int(*compare)(const void*,const void*)){uint8_t*p=(uint8_t*)base;for(size_t i=1;i<count;++i)for(size_t j=i;j&&compare(p+(j-1)*size,p+j*size)>0;--j)for(size_t k=0;k<size;++k){uint8_t t=p[(j-1)*size+k];p[(j-1)*size+k]=p[j*size+k];p[j*size+k]=t;}}
extern "C" void *bsearch(const void*key,const void*base,size_t count,size_t size,int(*compare)(const void*,const void*)){size_t lo=0,hi=count;const uint8_t*p=(const uint8_t*)base;while(lo<hi){size_t mid=lo+(hi-lo)/2;int c=compare(key,p+mid*size);if(!c)return(void*)(p+mid*size);if(c<0)hi=mid;else lo=mid+1;}return 0;}

static void append_char(char*dst,size_t cap,size_t*at,char c){if(*at+1<cap)dst[*at]=c;(*at)++;}
static void append_unsigned(char*dst,size_t cap,size_t*at,uint64_t value,unsigned base,int negative){char tmp[32];size_t n=0;if(!value)tmp[n++]='0';while(value&&n<sizeof(tmp)){unsigned d=(unsigned)(value%base);tmp[n++]=(char)(d<10?'0'+d:'a'+d-10);value/=base;}if(negative)append_char(dst,cap,at,'-');while(n)append_char(dst,cap,at,tmp[--n]);}
extern "C" int vsnprintf(char*dst,size_t cap,const char*fmt,va_list args){size_t at=0;for(size_t i=0;fmt[i];++i){if(fmt[i]!='%'){append_char(dst,cap,&at,fmt[i]);continue;}++i;if(fmt[i]=='%'){append_char(dst,cap,&at,'%');continue;}while(fmt[i]=='-'||fmt[i]=='+'||fmt[i]==' '||fmt[i]=='#'||fmt[i]=='0')++i;int precision=-1;if(fmt[i]=='.'){++i;precision=0;if(fmt[i]=='*'){precision=va_arg(args,int);++i;}else while(isdigit(fmt[i]))precision=precision*10+(fmt[i++]-'0');}while(fmt[i]=='l'||fmt[i]=='z'||fmt[i]=='t'||fmt[i]=='h')++i;char c=fmt[i];if(c=='s'){const char*s=va_arg(args,const char*);if(!s)s="(null)";for(int n=0;*s&&(precision<0||n<precision);++n)append_char(dst,cap,&at,*s++);}else if(c=='c')append_char(dst,cap,&at,(char)va_arg(args,int));else if(c=='d'||c=='i'){long long v=va_arg(args,int);append_unsigned(dst,cap,&at,v<0?(uint64_t)-v:(uint64_t)v,10,v<0);}else if(c=='u')append_unsigned(dst,cap,&at,va_arg(args,unsigned),10,0);else if(c=='x'||c=='X')append_unsigned(dst,cap,&at,va_arg(args,unsigned),16,0);else if(c=='p'){append_char(dst,cap,&at,'0');append_char(dst,cap,&at,'x');append_unsigned(dst,cap,&at,(uintptr_t)va_arg(args,void*),16,0);}else append_char(dst,cap,&at,c);}if(cap)dst[at<cap?at:cap-1]=0;return(int)at;}
extern "C" int snprintf(char*dst,size_t cap,const char*fmt,...){va_list args;va_start(args,fmt);int result=vsnprintf(dst,cap,fmt,args);va_end(args);return result;}
extern "C" int sscanf(const char*text,const char*format,...){(void)text;(void)format;return 0;}
extern "C" int sprintf(char*dst,const char*fmt,...){va_list args;va_start(args,fmt);int result=vsnprintf(dst,(size_t)-1,fmt,args);va_end(args);return result;}extern "C" int vfprintf(FILE*stream,const char*fmt,va_list args){(void)stream;char buffer[512];return vsnprintf(buffer,sizeof(buffer),fmt,args);}extern "C" int fprintf(FILE*stream,const char*fmt,...){va_list args;va_start(args,fmt);int result=vfprintf(stream,fmt,args);va_end(args);return result;}
extern "C" FILE*fopen(const char*path,const char*mode){(void)path;(void)mode;return 0;}extern "C" int fclose(FILE*stream){(void)stream;return 0;}extern "C" char*fgets(char*dst,int cap,FILE*stream){(void)dst;(void)cap;(void)stream;return 0;}
extern "C" int fputs(const char*text,FILE*stream){(void)stream;return(int)strlen(text);}extern "C" int fputc(int value,FILE*stream){(void)stream;return value;}extern "C" size_t fread(void*dst,size_t size,size_t count,FILE*stream){(void)dst;(void)size;(void)count;(void)stream;return 0;}extern "C" size_t fwrite(const void*src,size_t size,size_t count,FILE*stream){(void)src;(void)stream;return size*count;}extern "C" int fseek(FILE*stream,long offset,int origin){(void)stream;(void)offset;(void)origin;return -1;}extern "C" long ftell(FILE*stream){(void)stream;return -1;}extern "C" int feof(FILE*stream){(void)stream;return 1;}extern "C" int ferror(FILE*stream){(void)stream;return 0;}extern "C" int fflush(FILE*stream){(void)stream;return 0;}extern "C" void setbuf(FILE*stream,char*buffer){(void)stream;(void)buffer;}
extern "C" int remove(const char*path){(void)path;return -1;}extern "C" int rename(const char*old_path,const char*new_path){(void)old_path;(void)new_path;return -1;}
extern "C" void abort(void){__builtin_trap();}extern "C" void exit(int status){(void)status;__builtin_trap();}
extern "C" int atexit(void(*function)(void)){(void)function;return 0;}
extern "C" double fabs(double x){return x<0?-x:x;}extern "C" float fabsf(float x){return x<0?-x:x;}
extern "C" double floor(double x){long long i=(long long)x;return x<0&&x!=(double)i?(double)(i-1):(double)i;}extern "C" float floorf(float x){return(float)floor(x);}
extern "C" double ceil(double x){long long i=(long long)x;return x>0&&x!=(double)i?(double)(i+1):(double)i;}extern "C" float ceilf(float x){return(float)ceil(x);}
extern "C" double fmod(double x,double y){if(y==0)return 0;long long q=(long long)(x/y);return x-(double)q*y;}
static double reduce_angle(double x){x=fmod(x,2*M_PI);if(x>M_PI)x-=2*M_PI;else if(x<-M_PI)x+=2*M_PI;return x;}
extern "C" double sin(double x){x=reduce_angle(x);double x2=x*x;return x*(1-x2/6+x2*x2/120-x2*x2*x2/5040+x2*x2*x2*x2/362880);}
extern "C" double cos(double x){x=reduce_angle(x);double x2=x*x;return 1-x2/2+x2*x2/24-x2*x2*x2/720+x2*x2*x2*x2/40320;}
extern "C" double sqrt(double x){if(x<=0)return 0;double r=x>1?x:1;for(int i=0;i<20;++i)r=(r+x/r)*.5;return r;}
extern "C" double pow(double base,double exponent){long long whole=(long long)exponent;if(exponent!=(double)whole)return 0;bool inverse=whole<0;if(inverse)whole=-whole;double result=1;while(whole){if(whole&1)result*=base;base*=base;whole>>=1;}return inverse?1/result:result;}
extern "C" int gettimeofday(timeval*value,void*zone){(void)zone;const sacx_api*api=dihscover_netsurf_api();uint64_t ticks=api?api->time_ticks():0;value->tv_sec=(time_t)(ticks/1000u);value->tv_usec=(long)((ticks%1000u)*1000u);return 0;}
extern "C" time_t time(time_t*value){timeval now;gettimeofday(&now,0);if(value)*value=now.tv_sec;return now.tv_sec;}extern "C" clock_t clock(void){const sacx_api*api=dihscover_netsurf_api();return(clock_t)(api?api->time_ticks():0);}
extern "C" char *ctime(const time_t*value){(void)value;static char text[]="Thu Jan  1 00:00:00 1970\n";return text;}
extern "C" tm *gmtime(const time_t*value){(void)value;static tm result={0,0,0,1,0,70,4,0,0};return &result;}extern "C" tm *localtime(const time_t*value){return gmtime(value);}extern "C" time_t mktime(tm*value){(void)value;return 0;}
extern "C" size_t strftime(char*dst,size_t cap,const char*format,const tm*value){(void)format;(void)value;const char*text="1970-01-01";size_t n=strlen(text);if(cap){size_t copy=n<cap-1?n:cap-1;memcpy(dst,text,copy);dst[copy]=0;}return n;}
extern "C" char *setlocale(int category,const char*locale){(void)category;(void)locale;static char c[]="C";return c;}extern "C" int access(const char*path,int mode){(void)path;(void)mode;return -1;}
extern "C" int stat(const char*path,struct stat*result){(void)path;if(result)memset(result,0,sizeof(*result));return -1;}extern "C" int mkdir(const char*path,mode_t mode){(void)path;(void)mode;return -1;}extern "C" int unlink(const char*path){(void)path;return -1;}extern "C" int rmdir(const char*path){(void)path;return -1;}
extern "C" int uname(struct utsname*result){if(!result)return -1;memset(result,0,sizeof(*result));strcpy(result->sysname,"DihOS");strcpy(result->nodename,"dihos");strcpy(result->release,"1");strcpy(result->version,"1");strcpy(result->machine,"aarch64");return 0;}
struct dihs_dir{int unused;};extern "C" DIR*opendir(const char*path){(void)path;return 0;}extern "C" dirent*readdir(DIR*directory){(void)directory;return 0;}extern "C" int closedir(DIR*directory){(void)directory;return 0;}extern "C" int dirfd(DIR*directory){(void)directory;return -1;}extern "C" int fstatat(int directory,const char*path,struct stat*result,int flags){(void)directory;(void)flags;return stat(path,result);}extern "C" int unlinkat(int directory,const char*path,int flags){(void)directory;(void)flags;return unlink(path);}
extern "C" ssize_t pread(int fd,void*buffer,size_t count,off_t offset){(void)fd;(void)buffer;(void)count;(void)offset;return -1;}extern "C" ssize_t pwrite(int fd,const void*buffer,size_t count,off_t offset){(void)fd;(void)buffer;(void)count;(void)offset;return -1;}
extern "C" int regcomp(regex_t*regex,const char*pattern,int flags){(void)regex;(void)pattern;(void)flags;return REG_NOMATCH;}extern "C" int regexec(const regex_t*regex,const char*text,size_t count,regmatch_t matches[],int flags){(void)regex;(void)text;(void)count;(void)matches;(void)flags;return REG_NOMATCH;}extern "C" void regfree(regex_t*regex){(void)regex;}
extern "C" size_t regerror(int error,const regex_t*regex,char*buffer,size_t capacity){(void)error;(void)regex;const char*text="regex unavailable";size_t n=strlen(text)+1;if(capacity){size_t copy=n<capacity?n:capacity;memcpy(buffer,text,copy);buffer[copy-1]=0;}return n;}
extern "C" sighandler_t signal(int number,sighandler_t handler){(void)number;return handler;}
extern "C" int inflateInit2_(z_stream*stream,int bits,const char*version,int size){(void)stream;(void)bits;(void)version;(void)size;return Z_DATA_ERROR;}extern "C" int inflate(z_stream*stream,int flush){(void)stream;(void)flush;return Z_DATA_ERROR;}extern "C" int inflateEnd(z_stream*stream){(void)stream;return Z_OK;}extern "C" gzFile gzopen(const char*path,const char*mode){(void)path;(void)mode;return 0;}extern "C" char*gzgets(gzFile file,char*buffer,int capacity){(void)file;(void)buffer;(void)capacity;return 0;}extern "C" int gzclose(gzFile file){(void)file;return Z_OK;}
extern "C" iconv_t iconv_open(const char*to,const char*from){(void)to;(void)from;return(iconv_t)1;}extern "C" size_t iconv(iconv_t converter,char**input,size_t*input_left,char**output,size_t*output_left){(void)converter;size_t n=*input_left<*output_left?*input_left:*output_left;memcpy(*output,*input,n);*input+=n;*output+=n;*input_left-=n;*output_left-=n;return *input_left?((size_t)-1):0;}extern "C" int iconv_close(iconv_t converter){(void)converter;return 0;}
