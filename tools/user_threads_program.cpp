#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include <stddef.h>
extern "C" void *malloc(size_t);
extern "C" void free(void*);
extern "C" void *realloc(void*,size_t);
#if USER_THREAD_KIND==5
#define TLS_INITIAL 0
thread_local uint64_t local_value;
#else
#define TLS_INITIAL 0x1234
thread_local uint64_t local_value=TLS_INITIAL;
#endif
thread_local uint64_t local_zero;
static uint32_t gate,entered,timeout_word;
static uint64_t pointers[4],ids[4],stack_addresses[4];
static uint32_t spin_stop;
static volatile uint64_t spin_counts[3];
static void require(bool condition,int code,const char *text){if(!condition)sacx_user_exit(code,text);}
static uint64_t quick(uint64_t argument){
    require(local_value==TLS_INITIAL && !local_zero,172,"Thread TLS reuse was not initialized");
    return argument+32;
}
static uint64_t revoke_join_output(uint64_t address){
    sacx_user_call(SACX_USER_YIELD,0,0,0);
    require(!sacx_user_call(SACX_USER_MEMORY_PROTECT,address,4096,1),178,"Join output protection setup failed");return 0xbeef;
}
static uint64_t invalidated_wait(uint64_t address){
    require(sacx_user_wait32((uint32_t*)(uintptr_t)address,0,60000)==-14,179,"Invalidated wait word was not rejected");return 80;
}
static uint64_t spin_worker(uint64_t index){
    require(local_value==TLS_INITIAL && !local_zero,162,"Spin worker TLS initialization failed");
    local_value+=index;
    while(!__atomic_load_n(&spin_stop,__ATOMIC_ACQUIRE))spin_counts[index]=spin_counts[index]+1;
    require(local_value==TLS_INITIAL+index,166,"Preempted worker TLS corrupted");return spin_counts[index];
}
static uint64_t worker(uint64_t argument){
#if USER_THREAD_KIND==2
    __asm__ volatile("mov x1,xzr; ldr x0,[x1]":::"x0","x1","memory");return 0;
#else
    uint64_t stack_variable=argument;
    require(local_value==TLS_INITIAL && !local_zero,162,"Thread TLS initialization failed");
    local_value+=argument;local_zero=argument*100;
    pointers[argument]=(uintptr_t)__builtin_thread_pointer();ids[argument]=sacx_user_thread_self();stack_addresses[argument]=(uintptr_t)&stack_variable;
    __atomic_fetch_add(&entered,1,__ATOMIC_RELEASE);
    int rc=sacx_user_wait32(&gate,0,60000);require(rc==0 || rc==-11,163,"Thread wait failed");
    const sacx_api *local_api=sacx_userspace_api();
    require(SACX_API_HAS(local_api,mem_alloc) && SACX_API_HAS(local_api,app_yield),180,"Concurrent SACX table initialization failed");
    for(unsigned repeat=0;repeat<100;++repeat){
        auto p=static_cast<unsigned char*>(malloc(32));require(p!=nullptr,164,"Thread allocator failed");
        for(unsigned i=0;i<32;++i)p[i]=(unsigned char)(argument+i);
        auto grown=static_cast<unsigned char*>(realloc(p,256));require(grown!=nullptr,164,"Thread realloc failed");
        for(unsigned i=0;i<32;++i)require(grown[i]==(unsigned char)(argument+i),165,"Thread heap data corrupted");
        free(grown);if(!(repeat%8))sacx_user_call(SACX_USER_YIELD,0,0,0);
    }
    require(local_value==TLS_INITIAL+argument && local_zero==argument*100,166,"Thread TLS was shared");
    return 100+argument;
#endif
}
extern "C" int dihos_user_main(const sacx_api *api){
    require(local_value==TLS_INITIAL && !local_zero,160,"Main TLS initialization failed");
#if USER_THREAD_KIND==4
    int64_t failed=sacx_user_thread_create(quick,0);
    require(failed==-12,175,"Thread admission failure not reported");api->log("SACX threads: allocation failure cleaned up");return 79;
#elif USER_THREAD_KIND==6
    sacx_user_thread handles[3];
    for(unsigned i=0;i<3;++i){int64_t id=sacx_user_thread_create(spin_worker,i);require(id>0,161,"Spin worker admission failed");handles[i]=(uint64_t)id;}
    require(!sacx_user_call(SACX_USER_SLEEP_MS,20,0,0),168,"Main sleep during worker preemption failed");
    __atomic_store_n(&spin_stop,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<3;++i){uint64_t result=0;require(!sacx_user_thread_join(handles[i],&result,60000) && result>0,173,"API-free worker did not progress");}
    api->log("SACX threads: API-free workers preempted beside main OK");return 80;
#elif USER_THREAD_KIND==2
    require(malloc(1024)!=nullptr,164,"Thread fault cleanup heap setup failed");
    int64_t handle=sacx_user_thread_create(worker,1);require(handle>0,161,"Fault worker admission failed");
    uint64_t result;sacx_user_thread_join((uint64_t)handle,&result,1000);
    sacx_user_exit(176,"Worker fault escaped isolation");
#else
    pointers[0]=(uintptr_t)__builtin_thread_pointer();ids[0]=sacx_user_thread_self();
    require(((uint64_t*)(uintptr_t)pointers[0])[1]==ids[0],160,"TCB identity failed");
    local_value=0x7777;local_zero=999;
    require(sacx_user_thread_join(ids[0],nullptr,0)==-35,167,"Self join not rejected");
    require(sacx_user_wait32(&timeout_word,1,1)==-11 && sacx_user_wait32(&timeout_word,0,0)==-110 &&
            sacx_user_wait32(&timeout_word,0,2)==-110,168,"Thread timed wait failed");
    require(sacx_user_wait32((uint32_t*)1,0,1)==-22 && sacx_user_wait32((uint32_t*)0,0,1)==-14 &&
            sacx_user_wake32(&timeout_word,0)==-22,169,"Thread wait bounds failed");
    require(sacx_user_call(SACX_USER_THREAD_CREATE,(uintptr_t)&gate,0,0)==-14,170,"Thread non-executable entry accepted");
    sacx_user_thread handles[3];
    for(unsigned i=0;i<3;++i){int64_t id=sacx_user_thread_create(worker,i+1);require(id>0,161,"Worker admission failed");handles[i]=(uint64_t)id;}
    require(sacx_user_thread_create(worker,1)==-11,171,"Thread limit not enforced");
    while(__atomic_load_n(&entered,__ATOMIC_ACQUIRE)!=3)api->app_yield();
#if USER_THREAD_KIND==3
    sacx_user_wait32(&gate,0,60000);sacx_user_exit(177,"Blocked thread fixture unexpectedly woke");
#endif
    uint64_t result=0xabcdef;
    require(sacx_user_thread_join(handles[0],&result,1)==-110 && result==0xabcdef,168,"Join timeout modified result");
    require(sacx_user_thread_join(handles[0],(uint64_t*)1,0)==-14,169,"Join output bounds failed");
    __atomic_store_n(&gate,1,__ATOMIC_RELEASE);require(sacx_user_wake32(&gate,3)>=0,163,"Wake failed");
    for(unsigned i=0;i<3;++i){
        require(!sacx_user_thread_join(handles[i],&result,60000) && result==101+i,173,"Thread join result failed");
        require(sacx_user_thread_join(handles[i],&result,0)==-3,174,"Stale thread handle accepted");
    }
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)
        require(pointers[i]!=pointers[j] && ids[i]!=ids[j],166,"Thread identities/TLS overlap");
    require(stack_addresses[1]!=stack_addresses[2] && stack_addresses[2]!=stack_addresses[3],166,"Thread stacks overlap");
    require(local_value==0x7777 && local_zero==999,166,"Main TLS corrupted");
    for(unsigned repeat=0;repeat<20;++repeat){
        uint64_t argument=repeat==19?UINT64_MAX-32:repeat;
        int64_t id=sacx_user_thread_create(quick,argument);require(id>0 && (uint64_t)id!=handles[0],161,"Thread generation reuse failed");
        require(!sacx_user_thread_join((uint64_t)id,&result,60000) && result==argument+32,173,"Reused thread join failed");
    }
    void *output=nullptr;require(!api->mem_alloc(4096,&output),164,"Join revalidation buffer allocation failed");
    int64_t late=sacx_user_thread_create(revoke_join_output,(uintptr_t)output);require(late>0,161,"Join revalidation worker admission failed");
    require(sacx_user_thread_join((uint64_t)late,(uint64_t*)output,60000)==-14,178,"Join did not revalidate delayed output");
    require(!sacx_user_call(SACX_USER_MEMORY_PROTECT,(uintptr_t)output,4096,3),178,"Join output restore failed");
    require(!sacx_user_thread_join((uint64_t)late,(uint64_t*)output,60000) && *(uint64_t*)output==0xbeef,178,"Join retry after bad output failed");
    require(!api->mem_free(output),178,"Join output release failed");
    void *wait_word=nullptr;require(!api->mem_alloc(4096,&wait_word),164,"Wait invalidation buffer allocation failed");
    late=sacx_user_thread_create(invalidated_wait,(uintptr_t)wait_word);require(late>0,161,"Wait invalidation worker admission failed");
    api->app_yield();require(!api->mem_free(wait_word),179,"Wait invalidation free failed");
    require(!sacx_user_thread_join((uint64_t)late,&result,60000) && result==80,179,"Invalidated wait cleanup failed");
    api->log("SACX threads: TLS, joins, timed waits and shared heap OK");return 78;
#endif
}
