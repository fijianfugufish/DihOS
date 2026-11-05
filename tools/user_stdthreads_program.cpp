#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <memory>
#include "sacx_user_api.h"
#include "sacx_user_keys.h"
#include "dihos_user_allocator.h"
static std::mutex mutex;
static std::condition_variable condition;
static bool go,notified;
static std::atomic<unsigned> entered,cpp_destructors,key_destructors;
static sacx_user_key key;
thread_local unsigned number;
thread_local bool cpp_destroyed;
static void require(bool ok,const char *text){if(!ok)sacx_user_exit(160,text);}
static void require_no_live_heap(){
    dihos_user_allocator_stats stats;dihos_user_allocator_get_stats(&stats);
    require(!stats.allocations && !stats.live_bytes && !dihos_user_allocator_check(),"std::thread leaked owned heap after join");
}
struct Local {
    unsigned index;int *owned;
    Local():index(number),owned(new int(42)){}
    ~Local(){require(index==number && *owned==42,"std::thread TLS destructor identity failed");delete owned;cpp_destroyed=true;cpp_destructors.fetch_add(1);}
};
thread_local Local local;
static void key_destructor(void *pointer){
    require(pointer==(void*)(uintptr_t)(number+1),"std::thread key value was shared");
    require(!sacx_user_key_get(key),"Key value not cleared before destructor");
    require(cpp_destroyed,"Key cleanup preceded C++ TLS cleanup");key_destructors.fetch_add(1);
}
static void worker(unsigned index,std::unique_ptr<int> payload){
    number=index;require(*payload==42 && *local.owned==42,"std::thread move-only argument/TLS failed");
    require(!sacx_user_key_get(key) && !sacx_user_key_set(key,(void*)(uintptr_t)(index+1)),"std::thread key initialization failed");
    entered.fetch_add(1);
    {std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[]{return go;});}
    auto begin=std::chrono::steady_clock::now();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(std::chrono::steady_clock::now()>=begin+std::chrono::milliseconds(2),"std::thread sleep returned early");
    require(sacx_user_key_get(key)==(void*)(uintptr_t)(index+1),"std::thread key lost across sleep");
}
static void parked(){for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));}
extern "C" int dihos_user_main(const sacx_api *api){
#if USER_STDTHREAD_KIND==2
    std::thread a(parked),b(parked),c(parked),too_many(parked);
    sacx_user_exit(160,"std::thread limit was not enforced");
#elif USER_STDTHREAD_KIND==3
    {std::thread unjoined(parked);}sacx_user_exit(160,"Joinable std::thread destructor was accepted");
#elif USER_STDTHREAD_KIND==4
    std::thread cannot_detach(parked);cannot_detach.detach();sacx_user_exit(160,"Unsupported std::thread detach succeeded");
#else
    require(std::thread::hardware_concurrency()==1,"std::thread reported unsupported user cores");
    require(!sacx_user_key_create(&key,key_destructor),"std::thread application key creation failed");
    auto main_id=std::this_thread::get_id();
    std::thread threads[3];std::thread::id ids[3];
    for(unsigned i=0;i<3;++i){
        std::thread temporary(worker,i,std::make_unique<int>(42));ids[i]=temporary.get_id();
        require(temporary.joinable() && ids[i]!=main_id,"std::thread identity failed");
        threads[i]=std::move(temporary);require(!temporary.joinable(),"Moved std::thread still joinable");
        for(unsigned j=0;j<i;++j)require(ids[i]!=ids[j],"std::thread IDs collided");
    }
    while(entered.load()!=3)std::this_thread::yield();
    // The low-level adapter must free its envelope on failed admission, even
    // when the caller handles the error instead of aborting the process.
    dihos_user_allocator_stats before,after;dihos_user_allocator_get_stats(&before);
    std::__libcpp_thread_t rejected=1234;
    int rejected_error=std::__libcpp_thread_create(&rejected,[](void*)->void*{return nullptr;},nullptr);
    dihos_user_allocator_get_stats(&after);
    require(rejected_error==11 && rejected==1234 && before.allocations==after.allocations && before.live_bytes==after.live_bytes,"Failed thread admission leaked callback or changed output");
    {std::lock_guard<std::mutex> lock(mutex);go=true;}condition.notify_all();
    for(auto &thread:threads){thread.join();require(!thread.joinable(),"Joined std::thread still joinable");}
    require(cpp_destructors.load()==3 && key_destructors.load()==3 && !sacx_user_key_get(key),"std::thread join preceded TLS/key cleanup");
    require(!sacx_user_key_delete(key),"std::thread application key deletion failed");
    require_no_live_heap();
    // Standard deferred exit notification must follow C++ TLS destruction.
    std::thread notification([]{
        number=9;(void)local.owned;
        std::unique_lock<std::mutex> lock(mutex);notified=true;
        std::notify_all_at_thread_exit(condition,std::move(lock));
    });
    {std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[]{return notified;});
        require(cpp_destructors.load()==4,"Thread-exit notification preceded C++ TLS destructor");}
    notification.join();
    require_no_live_heap();
    // Reuse native stack/TLS slots and the process-lifetime libc++ key.
    for(unsigned i=0;i<12;++i){std::thread thread([]{number=11;(void)local.owned;});thread.join();require_no_live_heap();}
    require(cpp_destructors.load()==16,"std::thread TLS destructor did not survive slot reuse");
    require(!dihos_user_allocator_trim(),"std::thread heap trim failed");
    dihos_user_allocator_get_stats(&after);require(!after.reserved && !after.allocations,"std::thread heap remained reserved before process exit");
    api->log("SACX std::thread: ownership, keys, TLS cleanup, sleep and exit notification OK");return 84;
#endif
}
