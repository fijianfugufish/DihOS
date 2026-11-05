#include "sacx_user_api.h"
#include "sacx_user_thread_context.h"
static uint32_t stop,entered,finished,word;
static uint32_t stop2;
static uint64_t count,count2;
static sacx_user_thread spinner;
static sacx_user_thread_context context;
static constexpr uint64_t marker=0x123456789abcde00ull;
static void checkAt(bool ok,unsigned line){
    if(ok)return;
    char text[]="SACX context check failed line 000";
    text[sizeof(text)-4]='0'+line/100%10;text[sizeof(text)-3]='0'+line/10%10;text[sizeof(text)-2]='0'+line%10;
    sacx_user_exit(190,text);
}
#define check(ok) checkAt((ok),__LINE__)
static void sleep(unsigned ms){check(!sacx_user_call(SACX_USER_SLEEP_MS,ms,0,0));}
static void yield(){check(!sacx_user_call(SACX_USER_YIELD,0,0,0));}
static uint64_t runSpin(uint64_t which){
    uint64_t oldControl,oldStatus,a,b;
    __asm__ volatile("mrs %0,fpcr; mrs %1,fpsr":"=r"(oldControl),"=r"(oldStatus));
    uint64_t control=2ull<<22,status=16;
    uint32_t *stopWord=which?&stop2:&stop;
    uint64_t *progress=which?&count2:&count;
    __asm__ volatile(
        "mov x19,%[marker]\nmov x20,%[marker]\nadd x20,x20,#1\n"
        "dup v8.2d,x19\nmsr fpcr,%[control]\nmsr fpsr,%[status]\n"
        "1: ldar w9,[%[stop]]\ncbnz w9,2f\n"
        "ldr x10,[%[count]]\nadd x10,x10,#1\nstr x10,[%[count]]\nb 1b\n"
        "2: mov %[a],x19\nmov %[b],x20\n"
        :[a]"=r"(a),[b]"=r"(b)
        :[marker]"r"(marker),[control]"r"(control),[status]"r"(status),[stop]"r"(stopWord),[count]"r"(progress)
        :"x9","x10","x19","x20","v8","memory");
    __asm__ volatile("msr fpcr,%0; msr fpsr,%1"::"r"(oldControl),"r"(oldStatus));
    check(a==marker && b==marker+1);return 123;
}
static uint64_t sleeper(uint64_t){
    __atomic_store_n(&entered,1,__ATOMIC_RELEASE);sleep(35);
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);return 41;
}
static uint64_t waiter(uint64_t){
    __atomic_store_n(&entered,1,__ATOMIC_RELEASE);
    check(!sacx_user_wait32(&word,0,3000));
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);return 42;
}
static uint64_t intruder(uint64_t main){
    sacx_user_thread_context local;
    check(sacx_user_thread_pause(main,&local)==-16);
    check(sacx_user_thread_resume(spinner)==-1);return 43;
}
static uint64_t inspector(uint64_t){
    sacx_user_thread_context local;check(!sacx_user_thread_pause(spinner,&local));
    return 44; // thread exit must release our stop automatically
}
static void join(sacx_user_thread thread,uint64_t expected){
    uint64_t result=0;check(!sacx_user_thread_join(thread,&result,3000));check(result==expected);
}
static void pauseAt(sacx_user_thread thread,uint32_t state){
    for(unsigned i=0;i<100;++i){
        check(!sacx_user_thread_pause(thread,&context));
        if(context.state==state)return;
        check(!sacx_user_thread_resume(thread));yield();
    }
    check(false);
}
extern "C" int dihos_user_main(const sacx_api *api){
    uint64_t self=sacx_user_thread_self();sacx_user_stack_bounds bounds;
    check(!sacx_user_thread_stack(self,&bounds));
    check(bounds.begin<reinterpret_cast<uintptr_t>(&bounds) && reinterpret_cast<uintptr_t>(&bounds)<bounds.end);
    check(bounds.end-bounds.begin==SACX_USER_THREAD_STACK_BYTES);
    check(sacx_user_thread_pause(self,&context)==-35);
    check(sacx_user_thread_pause(UINT64_MAX,&context)==-3);
    check(sacx_user_thread_stack(self,nullptr)==-14);
    int64_t handle=sacx_user_thread_create(runSpin,0);check(handle>0);spinner=handle;
    while(__atomic_load_n(&count,__ATOMIC_RELAXED)<100)yield();
    check(sacx_user_thread_pause(spinner,nullptr)==-14);
    check(sacx_user_call(SACX_USER_THREAD_PAUSE,spinner,(uintptr_t)&context,sizeof(context)-1)==-22);
    int64_t page=sacx_user_call(SACX_USER_MEMORY_RESERVE,4096,0,0);check(page>0);
    check(!sacx_user_call(SACX_USER_MEMORY_COMMIT,page,4096,3));
    check(sacx_user_thread_pause(spinner,(sacx_user_thread_context*)(uintptr_t)(page+4096-424))==-14);
    check(!sacx_user_call(SACX_USER_MEMORY_PROTECT,page,4096,1));
    check(sacx_user_thread_pause(spinner,(sacx_user_thread_context*)(uintptr_t)page)==-14);
    check(!sacx_user_call(SACX_USER_MEMORY_RELEASE,page,0,0));
    check(!sacx_user_thread_pause(spinner,&context));
    check(context.version==1 && context.bytes==sizeof(context) && context.reserved==0 && context.thread==spinner);
    check(context.x[19]==marker && context.x[20]==marker+1);
    check(context.q[16]==marker && context.q[17]==marker);
    check(context.fpcr==(2ull<<22) && context.fpsr==16);
    check(context.pc && context.tpidr && context.sp>=context.stack_begin && context.sp<=context.stack_end);
    check(sacx_user_thread_pause(spinner,&context)==-16);
    check(sacx_user_thread_join(spinner,nullptr,0)==-16);
    check(sacx_user_thread_detach(spinner)==-16);
    uint64_t before=__atomic_load_n(&count,__ATOMIC_RELAXED);
#ifdef USER_CONTEXT_CANCEL
    api->log("SACX context: cancellation parked with owned stop");
    for(;;)sleep(60000);
#endif
    // A GC inspector must hold several mutators, then release one without
    // accidentally releasing another or losing the remaining ownership.
    handle=sacx_user_thread_create(runSpin,1);check(handle>0);
    while(__atomic_load_n(&count2,__ATOMIC_RELAXED)<100)yield();
    sacx_user_thread_context second;
    check(!sacx_user_thread_pause(handle,&second));
    uint64_t before2=__atomic_load_n(&count2,__ATOMIC_RELAXED);
    sleep(10);check(__atomic_load_n(&count,__ATOMIC_RELAXED)==before && __atomic_load_n(&count2,__ATOMIC_RELAXED)==before2);
    check(!sacx_user_thread_resume(handle));sleep(10);
    check(__atomic_load_n(&count2,__ATOMIC_RELAXED)>before2 && __atomic_load_n(&count,__ATOMIC_RELAXED)==before);
    __atomic_store_n(&stop2,1,__ATOMIC_RELEASE);join(handle,123);
    sleep(15);check(__atomic_load_n(&count,__ATOMIC_RELAXED)==before);
    handle=sacx_user_thread_create(intruder,self);check(handle>0);join(handle,43);
    check(__atomic_load_n(&count,__ATOMIC_RELAXED)==before);
    check(!sacx_user_thread_resume(spinner));check(sacx_user_thread_resume(spinner)==-1);
    sleep(10);check(__atomic_load_n(&count,__ATOMIC_RELAXED)>before);
    handle=sacx_user_thread_create(inspector,0);check(handle>0);join(handle,44);
    before=__atomic_load_n(&count,__ATOMIC_RELAXED);sleep(10);
    check(__atomic_load_n(&count,__ATOMIC_RELAXED)>before);
    __atomic_store_n(&stop,1,__ATOMIC_RELEASE);join(spinner,123);
    check(sacx_user_thread_pause(spinner,&context)==-3);
    handle=sacx_user_thread_create(sleeper,0);check(handle>0);
    check(sacx_user_thread_pause(spinner,&context)==-3);
    while(!__atomic_load_n(&entered,__ATOMIC_ACQUIRE))yield();
    pauseAt(handle,SACX_USER_THREAD_SLEEPING);sleep(50);check(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE));
    check(!sacx_user_thread_resume(handle));join(handle,41);
    entered=finished=0;handle=sacx_user_thread_create(waiter,0);check(handle>0);
    while(!__atomic_load_n(&entered,__ATOMIC_ACQUIRE))yield();
    pauseAt(handle,SACX_USER_THREAD_WAITING);check(sacx_user_wake32(&word,1)==1);check(sacx_user_wake32(&word,1)==0);
    sleep(10);check(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE));
    check(!sacx_user_thread_resume(handle));join(handle,42);
    api->log("SACX context: real GPR/SIMD/FP snapshot, owned stops, wait recovery and inspector exit OK");return 96;
}
