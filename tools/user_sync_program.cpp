#include <mutex>
#include <atomic>
#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include "sacx_user_sync.hpp"
static std::mutex mutex,other;
static std::recursive_mutex recursive;
static std::once_flag once;
static std::atomic<unsigned> entered,once_calls;
static unsigned counter,published;
static sacx_user_condition condition;
static bool go;
static void require(bool ok,const char *text){if(!ok)sacx_user_exit(157,text);}
static void initialize(){
    require(once_calls.fetch_add(1)==0,"std::call_once executed twice");
    sacx_user_call(SACX_USER_SLEEP_MS,5,0,0);published=42;
}
static uint64_t worker(uint64_t index){
    entered.fetch_add(1);
    {std::unique_lock<std::mutex> lock(mutex);
        while(!go)require(!sacx_condition_wait(&condition,mutex.native_handle()),"Condition predicate wait failed");
    }
    std::call_once(once,initialize);require(published==42,"std::call_once publication failed");
    for(unsigned i=0;i<80;++i){
        std::scoped_lock lock(mutex,other);
        unsigned saved=counter;sacx_user_call(SACX_USER_YIELD,0,0,0);
        counter=saved+1;
    }
    recursive.lock();require(recursive.try_lock(),"Recursive try_lock failed");recursive.unlock();recursive.unlock();
    return index+100;
}
extern "C" int dihos_user_main(const sacx_api *api){
#if USER_SYNC_KIND==2
    // Controlled process abort for a wrong-owner unlock.
    mutex.unlock();sacx_user_exit(157,"std::mutex wrong-owner unlock accepted");
#else
    require(sacx_sync_self()==sacx_user_thread_self(),"Synchronization TCB identity mismatch");
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(!mutex.try_lock(),"std::mutex try_lock accepted locked mutex");
        uint64_t deadline=(uint64_t)sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0)+2;
        require(sacx_condition_wait(&condition,mutex.native_handle(),deadline)==110,"Condition timeout failed");
        require(mutex.native_handle()->owner==sacx_user_thread_self(),"Condition timeout did not reacquire mutex");
    }
    sacx_user_thread workers[3];
    for(unsigned i=0;i<3;++i){int64_t id=sacx_user_thread_create(worker,i);require(id>0,"Synchronization worker creation failed");workers[i]=(uint64_t)id;}
    while(entered.load()!=3 || __atomic_load_n(&condition.waiters,__ATOMIC_RELAXED)!=3)api->app_yield();
    require(std::__libcpp_condvar_destroy(&condition)==16,"Busy condition destruction accepted");
    {std::lock_guard<std::mutex> lock(mutex);go=true;require(!sacx_condition_notify(&condition,4),"Condition broadcast failed");}
    std::call_once(once,initialize);require(published==42,"Main once publication failed");
    for(unsigned i=0;i<3;++i){uint64_t result=0;require(!sacx_user_thread_join(workers[i],&result,60000) && result==100+i,"Synchronization worker join failed");}
    require(counter==240 && once_calls.load()==1,"C++ synchronization lost updates");
    require(!condition.waiters && !std::__libcpp_condvar_destroy(&condition),"Condition waiters leaked");
    require(mutex.try_lock(),"std::mutex did not recover");mutex.unlock();
    api->log("SACX libc++ sync: mutex, recursive mutex, call_once and condition waits OK");return 83;
#endif
}
