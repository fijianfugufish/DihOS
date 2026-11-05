#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#ifndef DIHOS_USER_SYNC_TEST
#define DIHOS_USER_SYNC_TEST
#endif
#include "sacx_user_sync.hpp"
#ifdef DIHOS_USER_KEYS_TEST
#include "sacx_user_keys.h"
extern "C" void dihos_user_thread_keys_finalize();
[[noreturn]] void dihos_user_key_panic(const char *text){std::fputs(text,stderr);std::abort();}
static sacx_user_key reinstall_key;
static std::atomic<unsigned> key_callbacks;
thread_local unsigned key_passes;
static void key_destructor(void *pointer){
    assert(pointer==(void*)42 && !sacx_user_key_get(reinstall_key));
    ++key_passes;key_callbacks.fetch_add(1);assert(!sacx_user_key_set(reinstall_key,pointer));
}
static void test_keys(){
    assert(!sacx_user_key_create(&reinstall_key,key_destructor));
    std::vector<std::thread> workers;
    for(unsigned index=0;index<4;++index)workers.emplace_back([index]{
        for(unsigned i=0;i<1000;++i){
            sacx_user_key key;assert(!sacx_user_key_create(&key,nullptr));
            auto value=(void*)(uintptr_t)(index+1);
            assert(!sacx_user_key_get(key) && !sacx_user_key_set(key,value));
            assert(sacx_user_key_get(key)==value);
            assert(!sacx_user_key_delete(key) && !sacx_user_key_get(key));
            assert(sacx_user_key_set(key,value)==22);
        }
        for(unsigned i=0;i<100;++i){
            key_passes=0;assert(!sacx_user_key_get(reinstall_key));
            assert(!sacx_user_key_set(reinstall_key,(void*)42));dihos_user_thread_keys_finalize();
            assert(key_passes==4 && !sacx_user_key_get(reinstall_key));
        }
    });
    for(auto &thread:workers)thread.join();
    assert(key_callbacks==1600 && !sacx_user_key_delete(reinstall_key));
    std::puts("PASS: four concurrent key users, 4000 generation reuses, thread isolation and 1600 destructor/reinstall callbacks");
}
#endif
static std::atomic<uint64_t> next_id{1};
uint64_t sacx_sync_self(){thread_local uint64_t id=next_id.fetch_add(1);return id;}
int64_t sacx_sync_now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Queue {std::condition_variable cv;};
static std::mutex wait_mutex;
static std::map<uint32_t*,std::unique_ptr<Queue>> queues;
static std::atomic<unsigned> waits,wakes;
int sacx_sync_wait(uint32_t *word,uint32_t expected,uint32_t ms){
    std::unique_lock lock(wait_mutex);++waits;
    if(__atomic_load_n(word,__ATOMIC_ACQUIRE)!=expected)return -11;
    auto &queue=queues[word];if(!queue)queue=std::make_unique<Queue>();
    auto status=queue->cv.wait_for(lock,std::chrono::milliseconds(ms));
    return status==std::cv_status::timeout?-110:0;
}
int sacx_sync_wake(uint32_t *word,uint32_t count){
    std::lock_guard lock(wait_mutex);++wakes;
    auto found=queues.find(word);if(found==queues.end())return 0;
    if(count==1)found->second->cv.notify_one();else found->second->cv.notify_all();return 0;
}
int main(){
    sacx_user_mutex mutex{};sacx_user_recursive_mutex recursive{};
    sacx_user_condition condition{};bool go=false;unsigned counter=0;
    assert(!sacx_mutex_lock(&mutex) && sacx_mutex_trylock(&mutex)==16);
    assert(sacx_mutex_lock(&mutex)==35);
    assert(sacx_condition_wait(&condition,&mutex,(uint64_t)sacx_sync_now()+2)==110);
    assert(mutex.owner==sacx_sync_self() && !condition.waiters);
    std::thread intruder([&]{assert(sacx_mutex_unlock(&mutex)==1);});intruder.join();
    assert(!sacx_mutex_unlock(&mutex));
    std::vector<std::thread> workers;
    for(unsigned n=0;n<4;++n)workers.emplace_back([&]{
        assert(!sacx_mutex_lock(&mutex));
        while(!go)assert(!sacx_condition_wait(&condition,&mutex));
        assert(!sacx_mutex_unlock(&mutex));
        for(unsigned i=0;i<10000;++i){
            assert(!sacx_mutex_lock(&mutex));++counter;assert(!sacx_mutex_unlock(&mutex));
            assert(!sacx_recursive_lock(&recursive));assert(!sacx_recursive_trylock(&recursive));
            assert(recursive.depth==2);assert(!sacx_recursive_unlock(&recursive));assert(!sacx_recursive_unlock(&recursive));
        }
    });
    while(__atomic_load_n(&condition.waiters,__ATOMIC_RELAXED)!=4)std::this_thread::yield();
    assert(!sacx_mutex_lock(&mutex));go=true;
    assert(!sacx_condition_notify(&condition,4));assert(!sacx_mutex_unlock(&mutex));
    for(auto &thread:workers)thread.join();
    assert(counter==40000 && !mutex.word && !recursive.base.word && !condition.waiters && waits>0 && wakes>0);
    // Notify between unlock and kernel comparison must not disappear. A very
    // short timeout also confirms reacquisition on the mismatched-word path.
    for(unsigned n=0;n<100;++n){
        assert(!sacx_mutex_lock(&mutex));
        std::thread notifier([&]{assert(!sacx_mutex_lock(&mutex));assert(!sacx_condition_notify(&condition,1));assert(!sacx_mutex_unlock(&mutex));});
        int rc=sacx_condition_wait(&condition,&mutex,(uint64_t)sacx_sync_now()+1000);
        assert(!rc && mutex.owner==sacx_sync_self());assert(!sacx_mutex_unlock(&mutex));notifier.join();
    }
    std::puts("PASS: four concurrent workers, 40000 protected updates, recursive contention, wrong-owner rejection, condition timeout/relock and 100 notify races");
#ifdef DIHOS_USER_KEYS_TEST
    test_keys();
#endif
}
