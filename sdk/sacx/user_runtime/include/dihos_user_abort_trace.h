#pragma once
#include <stdint.h>
#include <stddef.h>

// Only read frame records inside the current thread's kernel-reported stack.
// One message retains both the assertion and its callers in the last-log slot.
static inline void dihos_user_abort_message(char (&message)[257], const char *reason,
    uintptr_t caller, uintptr_t frame, uintptr_t begin, uintptr_t end)
{
    unsigned n=0;
    const char *prefix="libc++: ";while(*prefix)message[n++]=*prefix++;
    if(reason){size_t length=0;while(reason[length])++length;if(length>96)reason+=length-96;}
    if(reason)while(*reason && n<104)message[n++]=*reason++;
    const char *label="\ncallers:";while(*label)message[n++]=*label++;
    constexpr char hex[]="0123456789abcdef";
    const uintptr_t floor=frame;
    for(unsigned depth=0;depth<7;++depth){
        message[n++]=depth && !(depth%3)?'\n':' ';
        for(unsigned i=0;i<16;++i)message[n++]=hex[(caller>>((15-i)*4))&15];
        if(!frame || (frame&15) || frame<floor || frame<begin || frame>end || end-frame<2*sizeof(uintptr_t))break;
        const auto *record=reinterpret_cast<const uintptr_t*>(frame);
        caller=record[1];
        const uintptr_t next=record[0];
        // Skip our own duplicate LR, but never follow backward/cyclic records.
        if(next<=frame){frame=0;}else frame=next;
    }
    message[n]=0;
}
