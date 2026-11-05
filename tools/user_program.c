#include <stdint.h>
#if USER_PROGRAM_KIND>=4
#include "sacx_user_api.h"
#endif
static volatile uint64_t initialized=41;
static volatile uint64_t zeroed[128];
static uint64_t call(uint64_t number,uint64_t value){
    register uint64_t x0 __asm__("x0")=value,x8 __asm__("x8")=number;
    __asm__ volatile("svc #0":"+r"(x0):"r"(x8):"memory");return x0;
}
__attribute__((noreturn)) void _start(void){
    if(initialized!=41 || zeroed[127]){call(0,99);for(;;){}}
    zeroed[127]=1;
#if USER_PROGRAM_KIND==5 || USER_PROGRAM_KIND==6
    const sacx_api *api=sacx_userspace_api();void *allocation=0;
    if(!SACX_API_HAS(api,mem_alloc) || api->mem_alloc(12289,&allocation))sacx_user_exit(111,"SACX heap allocation failed");
    volatile uint8_t *p=allocation;
    for(unsigned i=0;i<16384;++i)if(p[i])sacx_user_exit(112,"SACX heap was not zeroed");
    p[0]=42;p[16383]=91;
    if(sacx_user_call(SACX_USER_MEMORY_PROTECT,(uintptr_t)p,4096,SACX_USER_MEMORY_READ))sacx_user_exit(113,"SACX read-only protect failed");
#if USER_PROGRAM_KIND==6
    p[0]=99; /* Must fault in EL0 and reclaim the still-owned region. */
    sacx_user_exit(114,"SACX read-only heap write escaped");
#else
    if(p[0]!=42 || sacx_user_call(SACX_USER_QUERY,(uintptr_t)p,sizeof(sacx_user_info),0)!=-14 ||
       sacx_user_call(SACX_USER_MEMORY_PROTECT,(uintptr_t)p,4096,0) ||
       sacx_user_call(SACX_USER_LOG,(uintptr_t)p,1,0)!=-14 ||
       sacx_user_call(SACX_USER_MEMORY_PROTECT,(uintptr_t)p,4096,SACX_USER_MEMORY_READ|SACX_USER_MEMORY_WRITE) ||
       p[0]!=42 || p[16383]!=91)sacx_user_exit(115,"SACX heap protection failed");
    if(sacx_user_call(SACX_USER_MEMORY_RELEASE,(uintptr_t)p+4096,0,0)!=-22 ||
       sacx_user_call(SACX_USER_MEMORY_PROTECT,(uintptr_t)&_start&~4095ull,4096,3)!=-14 ||
       sacx_user_call(SACX_USER_MEMORY_PROTECT,(uintptr_t)p,4096,7)!=-22 ||
       sacx_user_call(SACX_USER_MEMORY_COMMIT,(uintptr_t)p,4096,3)!=-17 ||
       sacx_user_call(SACX_USER_MEMORY_RESERVE,64*1024*1024+4096,0,0)!=-7)sacx_user_exit(116,"SACX heap bounds rejection failed");
    if(api->mem_free(allocation) || sacx_user_call(SACX_USER_LOG,(uintptr_t)p,1,0)!=-14 ||
       api->mem_free(allocation)!=-22)sacx_user_exit(117,"SACX heap release failed");
    for(unsigned repeat=0;repeat<8;++repeat){
        if(api->mem_alloc(4096,&allocation))sacx_user_exit(118,"SACX heap reuse allocation failed");
        p=allocation;if(p[0] || p[4095])sacx_user_exit(119,"SACX reused heap was not zeroed");
        p[0]=1;p[4095]=1;
        if(api->app_yield() || api->mem_free(allocation))sacx_user_exit(120,"SACX heap reuse free failed");
    }
    /* Exit owns both an uncommitted and a committed allocation. */
    if(sacx_user_call(SACX_USER_MEMORY_RESERVE,4096,0,0)<0 || api->mem_alloc(4096,&allocation))sacx_user_exit(121,"SACX exit cleanup setup failed");
    api->log("SACX heap: zeroing, protection, reuse and cleanup OK");api->app_exit(74,0);
#endif
#elif USER_PROGRAM_KIND==4
    const sacx_api *api=sacx_userspace_api();sacx_user_info info;
    if(sacx_user_call(SACX_USER_QUERY,(uintptr_t)&info,sizeof(info),0) || info.version!=1 || info.max_buffer!=256)
        sacx_user_exit(101,"SACX query failed");
    if(!SACX_API_HAS(api,app_yield) || !SACX_API_HAS(api,time_monotonic_ms) || SACX_API_HAS(api,file_open))
        sacx_user_exit(102,"SACX availability failed");
    if(sacx_user_call(SACX_USER_LOG,0,4,0)!=-14 || sacx_user_call(SACX_USER_LOG,UINT64_MAX,1,0)!=-14 ||
       sacx_user_call(SACX_USER_LOG,(uintptr_t)zeroed,257,0)!=-7 ||
       sacx_user_call(SACX_USER_QUERY,(uintptr_t)&_start,sizeof(info),0)!=-14 ||
       sacx_user_call(UINT64_MAX,0,0,0)!=-38)
        sacx_user_exit(103,"SACX invalid buffer rejection failed");
    uint64_t before=api->time_monotonic_ms();
    if(api->app_yield() || sacx_user_call(SACX_USER_SLEEP_MS,5,0,0) || api->time_monotonic_ms()<before+5)
        sacx_user_exit(104,"SACX clock or sleep failed");
    api->log("SACX bridge: buffers, clock and sleep OK");
    api->app_exit(73,0);
#elif USER_PROGRAM_KIND==2
    for(;;)++zeroed[0];
#elif USER_PROGRAM_KIND==3
    __asm__ volatile("mov x1,xzr; ldr x0,[x1]":::"x0","x1","memory");
#else
    call(1,0);call(2,5);initialized+=zeroed[127];call(0,initialized);
#endif
    for(;;){}
}
