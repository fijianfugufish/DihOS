#include "net/dihos_tls_build.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"
#include "memory/pmem.h"
#include "system/dihos_time.h"
#include "system/entropy.h"
#include "net/knet_link.h"
#include "kwrappers/string.h"
#define TLS_PAGE_BUDGET 4096u
#define TLS_MEMORY_TAG 0x544c534d454d4f52ull
static uint64_t allocated_pages;
void mbedtls_platform_zeroize(void* p,size_t bytes){volatile unsigned char* out=p;while(bytes--)*out++=0;}
mbedtls_ms_time_t mbedtls_ms_time(void){return (mbedtls_ms_time_t)dihos_time_monotonic_ms();}
typedef struct tls_block {uint64_t pages,tag;} tls_block;
uint64_t knet_tls_memory_pages(void){return allocated_pages;}
void* dihos_tls_calloc(size_t n,size_t bytes){
    knet_link_checkpoint();
    if(!n || !bytes || n>SIZE_MAX/bytes)return 0;
    size_t total=n*bytes;if(total>SIZE_MAX-4095-sizeof(tls_block))return 0;
    uint64_t pages=(total+sizeof(tls_block)+4095)/4096;
    if(pages>TLS_PAGE_BUDGET-allocated_pages)return 0;
    tls_block* block=pmem_alloc_pages(pages);if(!block)return 0;
    memset(block,0,pages*4096);block->pages=pages;block->tag=TLS_MEMORY_TAG;allocated_pages+=pages;return block+1;
}
void dihos_tls_free(void* p){
    if(!p)return;
    tls_block* block=(tls_block*)p-1;
    if(block->tag!=TLS_MEMORY_TAG || !block->pages || block->pages>allocated_pages)return;
    uint64_t pages=block->pages;memset(block,0,pages*4096);allocated_pages-=pages;pmem_free_pages(block,pages);
    knet_link_checkpoint();
}
int64_t dihos_tls_time(int64_t* out){uint64_t now=0;int64_t value=dihos_time_utc(&now)?-1:(int64_t)now;if(out)*out=value;return value;}
struct tm* mbedtls_platform_gmtime_r(const int64_t* seconds,struct tm* out){
    if(!seconds || !out || *seconds<0 || *seconds>253402300799ll)return 0;
    uint64_t days=(uint64_t)*seconds/86400,rest=(uint64_t)*seconds%86400,original=days;
    int year=1970;for(;;){unsigned leap=year%4==0 && (year%100!=0 || year%400==0),n=365+leap;if(days<n)break;days-=n;++year;}
    memset(out,0,sizeof(*out));out->tm_year=year-1900;out->tm_yday=(int)days;out->tm_wday=(original+4)%7;
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned month=0;while(month<11){unsigned n=lengths[month]+(month==1 && year%4==0 && (year%100!=0 || year%400==0));if(days<n)break;days-=n;++month;}
    out->tm_mon=month;out->tm_mday=(int)days+1;out->tm_hour=rest/3600;out->tm_min=rest/60%60;out->tm_sec=rest%60;return out;
}
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t* context,uint8_t* out,size_t bytes,size_t* got){
    (void)context;*got=0;knet_link_checkpoint();if(dihos_entropy_fill(out,bytes))return PSA_ERROR_INSUFFICIENT_ENTROPY;*got=bytes;return PSA_SUCCESS;
}
/* Bounded C99 formatter for library diagnostic strings; no console/file I/O. */
int dihos_tls_vsnprintf(char* out,size_t cap,const char* fmt,va_list args){
    size_t at=0;
#define PUT(c) do{if(cap && at+1<cap)out[at]=(c);++at;}while(0)
    while(*fmt){if(*fmt!='%'){PUT(*fmt++);continue;}++fmt;
        unsigned width=0;char pad=' ';if(*fmt=='0'){pad='0';++fmt;}while(*fmt>='0' && *fmt<='9'){if(width<1000)width=width*10+*fmt-'0';++fmt;}
        int length=0;if(*fmt=='z'){length=3;++fmt;}else while(*fmt=='l'){++length;++fmt;}
        char type=*fmt;if(!type)break;++fmt;
        if(type=='%'){PUT('%');continue;}if(type=='s'){const char* s=va_arg(args,const char*);if(!s)s="(null)";while(*s)PUT(*s++);continue;}
        if(type=='c'){PUT((char)va_arg(args,int));continue;}
        if(type=='d'||type=='i'||type=='u'||type=='x'||type=='X'){
            uint64_t value;int negative=0;
            if(type=='d'||type=='i'){int64_t v=length==3?(int64_t)va_arg(args,ptrdiff_t):length>=2?va_arg(args,long long):length==1?va_arg(args,long):va_arg(args,int);negative=v<0;value=negative?0-(uint64_t)v:(uint64_t)v;}
            else value=length==3?va_arg(args,size_t):length>=2?va_arg(args,unsigned long long):length==1?va_arg(args,unsigned long):va_arg(args,unsigned);
            char text[32];unsigned n=0,base=type=='x'||type=='X'?16:10;const char* digits=type=='X'?"0123456789ABCDEF":"0123456789abcdef";
            do{text[n++]=digits[value%base];value/=base;}while(value);
            if(negative)PUT('-');while(width>n+(unsigned)negative){PUT(pad);--width;}while(n)PUT(text[--n]);
        }else{PUT('%');PUT(type);}
    }
    if(cap)out[at<cap?at:cap-1]=0;return at>2147483647u?-1:(int)at;
#undef PUT
}
int dihos_tls_snprintf(char* out,size_t cap,const char* fmt,...){va_list args;va_start(args,fmt);int n=dihos_tls_vsnprintf(out,cap,fmt,args);va_end(args);return n;}
