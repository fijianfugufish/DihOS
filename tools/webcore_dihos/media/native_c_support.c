/* Missing C helpers scoped to the native media link. No host libc is used. */
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sacx_user_random.h>
#include <sacx_user_api.h>
#ifndef DIHOS_MEDIA_USE_EXISTING_C_HELPERS
size_t strspn(const char* text,const char* accepted)
{
    size_t n=0;
    for(;text[n];++n){const char* p=accepted;while(*p && *p!=text[n])++p;if(!*p)break;}
    return n;
}
size_t strcspn(const char* text,const char* rejected)
{
    size_t n=0;
    for(;text[n];++n){const char* p=rejected;while(*p && *p!=text[n])++p;if(*p)break;}
    return n;
}
void* bsearch(const void* key,const void* base,size_t count,size_t size,
              int (*compare)(const void*,const void*))
{
    if(!size || count>SIZE_MAX/size)return NULL;
    size_t low=0,high=count;
    while(low<high){size_t mid=low+(high-low)/2;
        const void* item=(const unsigned char*)base+mid*size;int order=compare(key,item);
        if(!order)return (void*)item;
        if(order<0)high=mid;else low=mid+1;
    }
    return NULL;
}
#endif
long long llabs(long long value){return value<0?-value:value;}
int fputs(const char* text,FILE* stream)
{ size_t length=strlen(text);return fwrite(text,1,length,stream)==length?0:EOF; }
/* FFmpeg uses this for codec/demuxer seeds. Fail closed if the kernel entropy
 * service is unavailable; never substitute clock/address-derived randomness. */
uint32_t av_get_random_seed(void)
{
    uint32_t seed;
    if(sacx_user_random(&seed,sizeof(seed)))sacx_user_exit(188,"Video: trusted entropy unavailable");
    return seed;
}
/* FFmpeg's timestamp metadata uses these numeric UTC fields. Other locale
 * directives are deliberately unsupported and return zero (no metadata). */
size_t dihos_media_strftime(char* output,size_t capacity,const char* format,const struct tm* time)
{
    if(!capacity)return 0;
    output[0]=0;size_t length=0;
    for(;*format;++format){
        char digits[12];const char* part=digits;size_t count=1;digits[0]=*format;
        if(*format=='%'){
            int64_t value=0;int width=2;
            switch(*++format){
            case '%':digits[0]='%';break;
            case 'Y':value=(int64_t)time->tm_year+1900;width=4;goto number;
            case 'm':value=(int64_t)time->tm_mon+1;goto number;
            case 'd':value=time->tm_mday;goto number;
            case 'H':value=time->tm_hour;goto number;
            case 'M':value=time->tm_min;goto number;
            case 'S':value=time->tm_sec;goto number;
            default:output[0]=0;return 0;
            number:
                if(value<0 || value>9999){output[0]=0;return 0;}
                count=(size_t)width;
                for(int i=width-1;i>=0;--i){digits[i]=(char)('0'+value%10);value/=10;}
                if(value){output[0]=0;return 0;}
            }
        }
        if(count>=capacity-length){output[0]=0;return 0;}
        memcpy(output+length,part,count);length+=count;
    }
    output[length]=0;return length;
}
