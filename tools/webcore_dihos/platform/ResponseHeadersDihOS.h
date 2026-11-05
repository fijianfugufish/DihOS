#pragma once
#include <stddef.h>
#include <stdint.h>
/* Bounded HTTP field extraction. Values are octets (Latin-1 for WebCore), not
 * UTF-8 text. Empty field values and horizontal whitespace are valid. */
struct DihosResponseField { size_t name,nameBytes,value,valueBytes,next; };
static inline int dihos_response_field(const uint8_t* raw,size_t bytes,size_t offset,DihosResponseField* field)
{
    if(!raw || !field || offset>=bytes)return -1;
    size_t end=offset;
    while(end+1<bytes && !(raw[end]=='\r' && raw[end+1]=='\n'))++end;
    if(end+1>=bytes)return -1;
    if(end==offset)return end+2==bytes?1:-1;
    size_t colon=offset;while(colon<end && raw[colon]!=':')++colon;
    if(colon==offset || colon==end)return -1;
    for(size_t i=offset;i<colon;++i){
        uint8_t c=raw[i];int token=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');
        const char* extra="!#$%&'*+-.^_`|~";
        for(size_t j=0;extra[j];++j)if(c==(uint8_t)extra[j])token=1;
        if(!token)return -1;
    }
    for(size_t i=colon+1;i<end;++i)if((raw[i]<32 && raw[i]!='\t') || raw[i]==127)return -1;
    size_t begin=colon+1,tail=end;
    while(begin<tail && (raw[begin]==' '||raw[begin]=='\t'))++begin;
    while(tail>begin && (raw[tail-1]==' '||raw[tail-1]=='\t'))--tail;
    *field={offset,colon-offset,begin,tail-begin,end+2};return 0;
}
