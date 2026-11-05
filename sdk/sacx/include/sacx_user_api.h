#pragma once
#include "sacx_api.h"
#include "sacx_user_syscall.h"
/* A userspace-local function table. No kernel function pointers cross EL0.
 * Unsupported SACX members remain null; use SACX_API_HAS before calling. */
static int sacx_user_yield(void){return (int)sacx_user_call(SACX_USER_YIELD,0,0,0);}
static uint64_t sacx_user_monotonic(void){int64_t r=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);return r<0?0:(uint64_t)r;}
static void sacx_user_log(const char *text){
    if(!text)return;unsigned n=0;while(n<=SACX_USER_BUFFER_MAX && text[n])++n;
    (void)sacx_user_call(SACX_USER_LOG,(uintptr_t)text,n,0);
}
static int sacx_user_mem_free(void *ptr){return (int)sacx_user_call(SACX_USER_MEMORY_RELEASE,(uintptr_t)ptr,0,0);}
static int sacx_user_mem_alloc(uint32_t bytes,void **out){
    if(!out)return -22;*out=0;
    if(!bytes)return -22;
    if(bytes>64u*1024u*1024u)return -7;
    uint64_t rounded=((uint64_t)bytes+4095)&~4095ull;
    int64_t result=sacx_user_call(SACX_USER_MEMORY_RESERVE,rounded,0,0);
    if(result<0)return (int)result;
    uint64_t address=(uint64_t)result;
    for(uint64_t n=0;n<rounded;){
        uint64_t chunk=rounded-n;if(chunk>65536)chunk=65536;
        result=sacx_user_call(SACX_USER_MEMORY_COMMIT,address+n,chunk,SACX_USER_MEMORY_READ|SACX_USER_MEMORY_WRITE);
        if(result){(void)sacx_user_call(SACX_USER_MEMORY_RELEASE,address,0,0);return (int)result;}
        n+=chunk;
    }
    *out=(void*)(uintptr_t)address;return 0;
}
__attribute__((noreturn)) static int sacx_user_exit(int status,const char *text){
    if(text)sacx_user_log(text);
    (void)sacx_user_call(SACX_USER_EXIT,(uint64_t)(int64_t)status,0,0);
    for(;;)sacx_user_yield();
}
static const sacx_api *sacx_userspace_api(void){
    static sacx_api api;static uint32_t ready;
    if(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=2){
      uint32_t expected=0;
      if(__atomic_compare_exchange_n(&ready,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
        api.abi_version=SACX_API_ABI_VERSION;api.struct_size=sizeof(api);
        api.app_yield=sacx_user_yield;api.app_exit=sacx_user_exit;
        api.time_monotonic_ms=sacx_user_monotonic;api.log=sacx_user_log;
        api.mem_alloc=sacx_user_mem_alloc;api.mem_free=sacx_user_mem_free;
        if(__atomic_exchange_n(&ready,2,__ATOMIC_RELEASE)==3)
            (void)sacx_user_call(SACX_USER_WAKE32,(uintptr_t)&ready,4,0);
      }else{
        while(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=2){
            expected=1;(void)__atomic_compare_exchange_n(&ready,&expected,3,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED);
            if(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=2){
                int64_t rc=sacx_user_call(SACX_USER_WAIT32,(uintptr_t)&ready,3,60000);
                if(rc==-1 || rc==-38)(void)sacx_user_call(SACX_USER_YIELD,0,0,0);
            }
        }
      }
    }
    return &api;
}
