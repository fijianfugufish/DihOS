#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include <stdint.h>
extern "C" int __cxa_atexit(void (*)(void*),void*,void*);
extern "C" void __cxa_finalize(void*);
extern "C" int __cxa_thread_atexit_impl(void (*)(void*),void*,void*);
extern "C" int __cxa_guard_acquire(uint64_t*);
extern "C" void __cxa_guard_release(uint64_t*);
extern "C" void __cxa_guard_abort(uint64_t*);
static void require(bool ok,const char *text){if(!ok)sacx_user_exit(154,text);}
static uint32_t gate,entered,constructors,static_destroyed,registered_calls,completed;
static unsigned events[4][4],event_count[4];
static int token;
thread_local unsigned context;
static void note(unsigned value){
    require(context<4 && event_count[context]<4,"C++ TLS destructor event overflow");
    events[context][event_count[context]++]=value;
}
static void tail(void*){note(3);}
struct Object {
    unsigned order;int *owned;
    explicit Object(unsigned value):order(value),owned(new int(42)){}
    ~Object(){
        require(*owned==42,"C++ TLS destructor ownership corrupted");delete owned;note(order);
        if(order==2)require(!__cxa_thread_atexit_impl(tail,nullptr,nullptr),"C++ TLS destructor re-registration failed");
    }
};
thread_local Object first(1),second(2);
static void touch_tls(){require(*first.owned==42 && *second.owned==42,"C++ dynamic TLS initialization failed");}
struct Shared {
    unsigned value;
    Shared(){
        require(__atomic_add_fetch(&constructors,1,__ATOMIC_RELAXED)==1,"C++ local static initialized twice");
        sacx_user_call(SACX_USER_SLEEP_MS,5,0,0);value=42;
    }
    ~Shared(){require(value==42,"C++ local static destructor lost state");++static_destroyed;}
};
__attribute__((noinline)) static Shared &shared(){static Shared value;return value;}
static void registered(void*){
    __atomic_fetch_add(&registered_calls,1,__ATOMIC_RELAXED);
    sacx_user_call(SACX_USER_YIELD,0,0,0);
}
static uint64_t worker(uint64_t index){
    context=(unsigned)index;
    __atomic_fetch_add(&entered,1,__ATOMIC_RELEASE);
    int rc=sacx_user_wait32(&gate,0,60000);require(!rc || rc==-11,"C++ worker gate failed");
    require(shared().value==42,"C++ local static publication failed");touch_tls();
    for(unsigned i=0;i<4;++i)require(!__cxa_atexit(registered,nullptr,&token),"C++ concurrent exit registration failed");
    // Competing finalizers exercise exactly-once removal and unlocked callbacks.
    __cxa_finalize(&token);
    if(index==1)sacx_user_thread_exit(101);
    return 100+index;
}
static void main_checks(void*){
    require(completed && static_destroyed==1,"C++ process destructor ordering failed");
    require(constructors==1 && registered_calls==12,"C++ concurrent destructor registry lost entries");
    for(unsigned i=0;i<4;++i)require(event_count[i]==3 && events[i][0]==2 && events[i][1]==3 && events[i][2]==1,"C++ thread destructor order/cleanup failed");
    sacx_user_log("SACX C++ lifetime: static guards, TLS destructors and concurrent finalization OK");
}
#if USER_LIFETIME_KIND==2
__attribute__((noinline)) static int recursive(){static int value=recursive();return value;}
#endif
#if USER_LIFETIME_KIND==4
template<unsigned N> static void exhaust(){
    thread_local Object value(1);require(*value.owned==42,"C++ TLS overflow fixture initialization failed");
    if constexpr(N)exhaust<N-1>();
}
#endif
static void empty(void*){}
extern "C" int dihos_user_main(const sacx_api *api){
#if USER_LIFETIME_KIND==2
    (void)recursive();sacx_user_exit(154,"C++ recursive static was accepted");
#elif USER_LIFETIME_KIND==3
    // A bounded registry rejects exhaustion without corrupting existing entries.
    for(unsigned i=0;i<32;++i)require(!__cxa_thread_atexit_impl(empty,nullptr,nullptr),"C++ TLS registry filled too early");
    require(__cxa_thread_atexit_impl(empty,nullptr,nullptr)==-1,"C++ TLS registry overflow accepted");
    require(__cxa_thread_atexit_impl(nullptr,nullptr,nullptr)==-1,"C++ null TLS destructor accepted");
    dihos_user_thread_finalize();dihos_user_thread_finalize();
    require(!__cxa_thread_atexit_impl(empty,nullptr,nullptr),"C++ TLS registry did not recover");
    api->log("SACX C++ lifetime: bounded TLS destructor registry and reuse OK");return 82;
#elif USER_LIFETIME_KIND==4
    exhaust<32>();sacx_user_exit(154,"C++ compiler TLS registration failure was ignored");
#else
    require(!__cxa_atexit(main_checks,nullptr,nullptr),"C++ lifetime observer registration failed");
    // Aborted initialization can be retried; release publishes the first byte.
    alignas(8) uint64_t guard=0;
    require(__cxa_guard_acquire(&guard)==1,"C++ guard admission failed");__cxa_guard_abort(&guard);
    require(__cxa_guard_acquire(&guard)==1,"C++ aborted guard cannot retry");__cxa_guard_release(&guard);
    require(__cxa_guard_acquire(&guard)==0 && (guard&1),"C++ completed guard reacquired");
    sacx_user_thread handles[3];
    for(unsigned i=0;i<3;++i){int64_t handle=sacx_user_thread_create(worker,i);require(handle>0,"C++ lifetime worker creation failed");handles[i]=(uint64_t)handle;}
    while(__atomic_load_n(&entered,__ATOMIC_ACQUIRE)!=3)api->app_yield();
    __atomic_store_n(&gate,1,__ATOMIC_RELEASE);sacx_user_wake32(&gate,3);
    context=3;require(shared().value==42,"C++ main static access failed");touch_tls();
    for(unsigned i=0;i<3;++i){uint64_t result=0;require(!sacx_user_thread_join(handles[i],&result,60000) && result==100+i,"C++ lifetime join failed");
        require(event_count[i]==3,"C++ join returned before TLS destructors");}
    __cxa_finalize(&token);require(registered_calls==12,"C++ finalization was not exactly once");
    completed=1;return 81;
#endif
}
