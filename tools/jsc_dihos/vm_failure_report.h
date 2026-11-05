#pragma once
#include <stdint.h>
#include <stddef.h>
namespace DihOSVM {
struct Failure {
    const char* operation;int result;
    uintptr_t address,bytes,caller;
    uint64_t privateBytes,reserved,committed,limit;
};
inline void describe(char (&out)[257],const Failure& f){
    unsigned n=0;
    auto text=[&](const char* s){while(*s && n<256)out[n++]=*s++;};
    auto hex=[&](uint64_t v){text("0x");for(unsigned i=0;i<16 && n<256;++i)out[n++]="0123456789abcdef"[(v>>((15-i)*4))&15];};
    text("WTF VM ");text(f.operation);text(" rc=");
    uint32_t magnitude=(uint32_t)f.result;
    if(f.result<0){text("-");magnitude=0u-magnitude;}
    char digits[11];unsigned count=0;do{digits[count++]=(char)('0'+magnitude%10);magnitude/=10;}while(magnitude);
    while(count && n<256)out[n++]=digits[--count];
    text("\naddr=");hex(f.address);text(" bytes=");hex(f.bytes);text(" caller=");hex(f.caller);
    text("\nprivate=");hex(f.privateBytes);text(" reserved=");hex(f.reserved);
    text("\ncommitted=");hex(f.committed);text(" limit=");hex(f.limit);
    out[n]=0;
}
}
