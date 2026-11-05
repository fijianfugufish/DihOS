#pragma once
#include <stdint.h>
#include "../../../sdk/sacx/include/sacx_http_limits.h"
/* CRLF-terminated fields, no terminating empty line. No folded values, control
 * bytes, duplicate names or hop-by-hop/framing overrides. Shared by admission
 * and wire construction; validation never relies on a userspace NUL. */
#define DIHOS_HTTP_REQUEST_HEADERS_MAX SACX_HTTP_REQUEST_HEADERS_MAX
static inline int dihos_http_header_name_equal(const char *s,uint32_t n,const char *name)
{
    uint32_t i=0;
    for(;i<n && name[i];++i){unsigned c=(unsigned char)s[i];if(c>='A' && c<='Z')c+=32;if(c!=(unsigned char)name[i])return 0;}
    return i==n && !name[i];
}
static inline int dihos_http_header_token(unsigned c)
{
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))return 1;
    const char *p="!#$%&'*+-.^_`|~";while(*p)if(c==(unsigned char)*p++)return 1;return 0;
}
static inline int dihos_http_validate_request_headers(const char *s,uint32_t bytes)
{
    if(bytes>DIHOS_HTTP_REQUEST_HEADERS_MAX || (bytes&&!s))return -22;
    uint32_t at=0,fields=0;
    while(at<bytes){
        if(++fields>128)return -22;
        uint32_t begin=at;
        while(at<bytes && dihos_http_header_token((unsigned char)s[at]))++at;
        uint32_t name_bytes=at-begin;
        if(!name_bytes || at>=bytes || s[at++]!=':')return -22;
        const char *blocked[]={"host","connection","content-length","transfer-encoding","upgrade","te","trailer","proxy-connection","keep-alive","accept-encoding","expect"};
        for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);++i)
            if(dihos_http_header_name_equal(s+begin,name_bytes,blocked[i]))return -95;
        /* Bound duplicate checking to 128 fields and the shared byte cap. */
        uint32_t previous=0;
        while(previous<begin){
            uint32_t end=previous;while(end<begin && s[end]!=':')++end;
            if(end-previous==name_bytes){
                uint32_t i=0;
                for(;i<name_bytes;++i){unsigned a=(unsigned char)s[previous+i],b=(unsigned char)s[begin+i];if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)break;}
                if(i==name_bytes)return -22;
            }
            while(previous<begin && s[previous]!='\n')++previous;
            ++previous;
        }
        while(at<bytes && s[at]!='\r'){
            unsigned c=(unsigned char)s[at++];if((c<32 && c!=9)||c==127)return -22;
        }
        if(bytes-at<2 || s[at]!='\r' || s[at+1]!='\n')return -22;
        at+=2;
    }
    return 0;
}
static inline int dihos_http_request_has_header(const char *s,uint32_t bytes,const char *name)
{
    uint32_t at=0;
    while(at<bytes){uint32_t begin=at;while(at<bytes && s[at]!=':')++at;
        if(dihos_http_header_name_equal(s+begin,at-begin,name))return 1;
        while(at<bytes && s[at]!='\n')++at;++at;}
    return 0;
}
