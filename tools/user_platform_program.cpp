#include "sacx_user_api.h"
#include "sacx_user_thread_context.h"
#include "sacx_user_resources.h"
#include "fenv.h"
#include <string.h>
extern "C" void *memmem(const void*,size_t,const void*,size_t);
static int64_t processId;
#define RESOURCE_STRING_INNER(n) #n
#define RESOURCE_STRING(n) RESOURCE_STRING_INNER(n)
#define check(ok) do { if(!(ok))sacx_user_exit(194,"SACX native platform fixture failed at line " RESOURCE_STRING(__LINE__)); } while(0)
static void yield(){check(!sacx_user_call(SACX_USER_YIELD,0,0,0));}
static sacx_user_resources resources(){
    sacx_user_resources r;
    check(!sacx_user_resources_get(&r));
    check(r.version==1 && r.bytes==sizeof(r) && r.flags==3);
    check(r.thread_user_ns<=r.process_user_ns && r.thread_kernel_ns<=r.process_kernel_ns);
    check(r.heap_committed_bytes<=r.heap_reserved_bytes && r.heap_reserved_bytes<=r.heap_limit_bytes);
    return r;
}
static void checkResources(){
    sacx_user_resources r0=resources();check(r0.private_bytes>=SACX_USER_THREAD_STACK_BYTES);
    check(sacx_user_call(SACX_USER_RESOURCES,0,sizeof(r0),0)==-14);
    check(sacx_user_call(SACX_USER_RESOURCES,(uintptr_t)&r0,sizeof(r0)-1,0)==-22);
    check(sacx_user_call(SACX_USER_RESOURCES,(uintptr_t)&r0,sizeof(r0),1)==-22);
    check(sacx_user_call(SACX_USER_RESOURCES,(uintptr_t)&checkResources,sizeof(r0),0)==-14);
    int64_t memory=sacx_user_call(SACX_USER_MEMORY_RESERVE,4096,0,0);check(memory>0);
    auto r1=resources();check(r1.heap_reserved_bytes==r0.heap_reserved_bytes+4096);
    check(r1.private_bytes>=r0.private_bytes+12288 && r1.heap_committed_bytes==r0.heap_committed_bytes);
    check(!sacx_user_call(SACX_USER_MEMORY_COMMIT,memory,4096,3));
    auto r2=resources();check(r2.heap_committed_bytes==r0.heap_committed_bytes+4096);
    check(!sacx_user_call(SACX_USER_MEMORY_PROTECT,memory,4096,0));
    auto hidden=resources();check(hidden.heap_committed_bytes==r2.heap_committed_bytes);
    check(!sacx_user_call(SACX_USER_MEMORY_DECOMMIT,memory,4096,0));
    auto r3=resources();check(r3.heap_committed_bytes==r0.heap_committed_bytes && r3.private_bytes>=r1.private_bytes);
    check(!sacx_user_call(SACX_USER_MEMORY_RELEASE,memory,0,0));
    auto r4=resources();check(r4.heap_reserved_bytes==r0.heap_reserved_bytes);
    check(r4.private_bytes+12288==r3.private_bytes);
    volatile uint64_t work=0;for(unsigned i=0;i<200000;++i)work=work*33+i;
    auto r5=resources();check(r5.thread_user_ns>r4.thread_user_ns && r5.thread_kernel_ns>0);
    int64_t start=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);auto before=resources();
    check(!sacx_user_call(SACX_USER_SLEEP_MS,50,0,0));auto after=resources();
    int64_t end=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);
    uint64_t active=(after.thread_user_ns-before.thread_user_ns)+(after.thread_kernel_ns-before.thread_kernel_ns);
    check(end>=start+50 && active/1000000+40<=(uint64_t)(end-start)+3);
}
static double rounded(double x){double result;__asm__ volatile("frinti %d0,%d1":"=w"(result):"w"(x));return result;}
static uint64_t roundingWorker(uint64_t mode){
    auto own=resources();check(own.thread_user_ns>0);
    check(sacx_user_call(SACX_USER_PROCESS_SELF,0,0,0)==processId);
    volatile unsigned char stack[96*1024];
    for(unsigned i=0;i<sizeof(stack);i+=4096)stack[i]=(unsigned char)(i/4096);
    check(fegetround()==FE_DOWNWARD);check(!fesetround(mode));
    for(unsigned i=0;i<32;++i){
        check(rounded(1.5)==(mode==FE_UPWARD?2:1));check(rounded(-1.5)==-1);
        yield();check(fegetround()==(int)mode);
    }
    for(unsigned i=0;i<sizeof(stack);i+=4096)check(stack[i]==i/4096);
    return mode;
}
static uint64_t detachedWorker(uint64_t selfDetach){
    if(selfDetach)check(!sacx_user_thread_detach(sacx_user_thread_self()));
    return 7;
}
extern "C" int dihos_user_main(const sacx_api *api){
    check(!strcmp("en_US","en_US") && strcmp("en","en-US")<0 && strcmp("\x80","\x7f")>0);
    checkResources();
    processId=sacx_user_call(SACX_USER_PROCESS_SELF,0,0,0);check(processId>0 && processId<=0x7fffffff);
    const unsigned char hay[]={0,1,2,0,1,3};const unsigned char needle[]={0,1,3};
    check(memmem(hay,sizeof(hay),needle,sizeof(needle))==hay+3);
    check(memmem(hay,sizeof(hay),hay,0)==hay);
    check(!memmem(hay,2,needle,sizeof(needle)));
    check(!memmem(hay,3,needle,sizeof(needle)));
    volatile unsigned char stack[96*1024];
    for(unsigned i=0;i<sizeof(stack);i+=4096)stack[i]=(unsigned char)(i/4096);
    int64_t before=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);
    int64_t utc=sacx_user_call(SACX_USER_UTC_MS,0,0,0);check(utc>0);
    int64_t monotonic=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);
    check(!sacx_user_call(SACX_USER_SLEEP_MS,12,0,0));
    int64_t before2=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);
    int64_t utc2=sacx_user_call(SACX_USER_UTC_MS,0,0,0),monotonic2=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);
    check(utc2>=utc+12 && monotonic2>=monotonic+12);
    // Each UTC sample falls between its two monotonic samples. The possible
    // anchor intervals must intersect even if the host deschedules a syscall.
    check(utc-monotonic<=utc2-before2 && utc2-monotonic2<=utc-before);
    fenv_t initial,held;check(!fegetenv(&initial));
    check(!fesetround(FE_DOWNWARD));check(fesetround(123)==1 && fegetround()==FE_DOWNWARD);
    check(!feclearexcept(FE_ALL_EXCEPT));check(!feraiseexcept(FE_INVALID|FE_INEXACT));
    check(fetestexcept(FE_ALL_EXCEPT)==(FE_INVALID|FE_INEXACT));
    check(!feholdexcept(&held) && !fetestexcept(FE_ALL_EXCEPT));
    check(!feupdateenv(&held) && fetestexcept(FE_ALL_EXCEPT)==(FE_INVALID|FE_INEXACT));
    fenv_t invalid=initial;invalid.fpcr|=1<<8;check(fesetenv(&invalid)==1);
    int64_t a=sacx_user_thread_create(roundingWorker,FE_UPWARD),b=sacx_user_thread_create(roundingWorker,FE_TOWARDZERO);
    check(a>0 && b>0);
    for(unsigned i=0;i<32;++i){check(rounded(1.5)==1 && rounded(-1.5)==-2);yield();check(fegetround()==FE_DOWNWARD);}
    uint64_t result;check(!sacx_user_thread_join(a,&result,3000) && result==FE_UPWARD);
    check(!sacx_user_thread_join(b,&result,3000) && result==FE_TOWARDZERO);
    check(!fesetenv(&initial));
    check(sacx_user_thread_detach(sacx_user_thread_self())==-22);
    for(unsigned i=0;i<100;++i){
        int64_t handle=sacx_user_thread_create(detachedWorker,i&1);check(handle>0);
        if(!(i&1))check(!sacx_user_thread_detach(handle));
        sacx_user_stack_bounds bounds;int error=0;
        for(unsigned poll=0;poll<100 && !error;++poll){error=sacx_user_thread_stack(handle,&bounds);if(!error)yield();}
        check(error==-3);check(sacx_user_thread_detach(handle)==-3);
    }
    for(unsigned i=0;i<sizeof(stack);i+=4096)check(stack[i]==i/4096);
    api->log("SACX platform: real UTC, FP rounding inheritance/isolation, large guarded stacks and 100 detached cleanups OK");return 97;
}
