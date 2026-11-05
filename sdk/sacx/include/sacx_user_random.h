#pragma once
#include "sacx_user_syscall.h"
#include <stddef.h>
/* Nonblocking. Returns -11 when trusted entropy is unavailable; never fills
 * from a timestamp or process address. Large requests are bounded chunks.
 * On error, discard the whole output: an earlier chunk may already be filled. */
static inline int sacx_user_random(void *output,size_t bytes){
    if(!bytes)return 0;
    if(!output || (uintptr_t)output>UINTPTR_MAX-bytes)return -22;
    unsigned char *p=(unsigned char*)output;
    while(bytes){size_t n=bytes>SACX_USER_BUFFER_MAX?SACX_USER_BUFFER_MAX:bytes;
        int64_t rc=sacx_user_call(SACX_USER_RANDOM,(uintptr_t)p,n,0);
        if(rc)return (int)rc;
        p+=n;bytes-=n;
    }
    return 0;
}
