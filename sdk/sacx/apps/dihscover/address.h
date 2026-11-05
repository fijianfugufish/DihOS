#pragma once
#include <stddef.h>

/* Shared by the NetSurf UI and the isolated WebCore UI. No widget or engine
   pointers cross this boundary. Return failure rather than navigate a truncated URL. */
static inline int dihscover_address_space(unsigned char c)
{ return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
static inline int dihscover_address_normalize(const char *input,char *out,size_t cap)
{
    if(!input || !out || !cap)return -1;
    out[0]=0;
    while(dihscover_address_space((unsigned char)*input))++input;
    size_t length=0;while(input[length])++length;
    while(length && dihscover_address_space((unsigned char)input[length-1]))--length;
    if(!length)return -1;
    int scheme=0,space=0,dot=0;
    for(size_t i=0;i<length;++i){
        unsigned char c=(unsigned char)input[i];
        if(c<32 || c==127)return -1;
        if(c==':' && i>0 && i<16){scheme=1;break;}
        if(dihscover_address_space(c))space=1;
        if(c=='.')dot=1;
    }
    const char *prefix=scheme?"":space || !dot?"https://www.google.com/search?q=":"https://";
    int search=!scheme && (space || !dot);size_t at=0;
    for(size_t i=0;prefix[i];++i){if(at+1>=cap)return -1;out[at++]=prefix[i];}
    static const char hex[]="0123456789ABCDEF";
    for(size_t i=0;i<length;++i){
        unsigned char c=(unsigned char)input[i];
        if(c<32 || c==127){out[0]=0;return -1;}
        int plain=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';
        size_t count=search && c!=' ' && !plain?3:1;
        if(count>=cap-at){out[0]=0;return -1;}
        if(search && c==' ')out[at++]='+';
        else if(count==3){out[at++]='%';out[at++]=hex[c>>4];out[at++]=hex[c&15];}
        else out[at++]=(char)c;
    }
    out[at]=0;return 0;
}
