#include "sacx_user_api.h"
#include "sacx_user_threads.h"
#include "sacx_user_keys.h"
static void require(bool ok,const char *text){if(!ok)sacx_user_exit(161,text);}
static sacx_user_key keys[16],reinstall_key;
static unsigned callbacks;
thread_local unsigned worker_callbacks;
static void reinstall(void *pointer){
    require(pointer==(void*)42 && !sacx_user_key_get(reinstall_key),"Key destructor value/clear failed");
    ++worker_callbacks;__atomic_fetch_add(&callbacks,1,__ATOMIC_RELAXED);
    require(!sacx_user_key_set(reinstall_key,pointer),"Key destructor reinstall failed");
}
static uint64_t worker(uint64_t){
    require(!worker_callbacks && !sacx_user_key_get(reinstall_key),"Key values survived thread reuse");
    require(!sacx_user_key_set(reinstall_key,(void*)42),"Key worker set failed");return 0;
}
extern "C" int dihos_user_main(const sacx_api *api){
    require(sacx_user_key_create(nullptr,nullptr)==22 && sacx_user_key_set(0,(void*)1)==22 && !sacx_user_key_get(0),"Invalid thread key accepted");
    for(unsigned i=0;i<16;++i)require(!sacx_user_key_create(&keys[i],nullptr),"Key capacity filled too early");
    sacx_user_key extra=0x123456;
    require(sacx_user_key_create(&extra,nullptr)==11 && extra==0x123456,"Key capacity overflow modified output");
    require(!sacx_user_key_set(keys[3],(void*)123),"Key initial set failed");
    auto stale=keys[3];require(!sacx_user_key_delete(stale) && sacx_user_key_delete(stale)==22,"Key deletion/stale rejection failed");
    require(!sacx_user_key_create(&keys[3],nullptr) && keys[3]!=stale && !sacx_user_key_get(keys[3]),"Key generation reuse exposed stale value");
    require(sacx_user_key_set(stale,(void*)1)==22 && !sacx_user_key_get(stale),"Deleted key reused authority");
    for(auto id:keys)require(!sacx_user_key_delete(id),"Key capacity cleanup failed");
    require(!sacx_user_key_create(&reinstall_key,reinstall),"Destructor key creation failed");
    for(unsigned i=0;i<6;++i){
        int64_t thread=sacx_user_thread_create(worker,0);require(thread>0,"Key worker creation failed");
        require(!sacx_user_thread_join((uint64_t)thread,nullptr,60000),"Key worker join failed");
        require(callbacks==4*(i+1),"Key destructor pass bound or join ordering failed");
    }
    require(callbacks==24 && !sacx_user_key_get(reinstall_key),"Worker key value leaked into main");
    require(!sacx_user_key_delete(reinstall_key),"Destructor key cleanup failed");
    api->log("SACX thread keys: isolation, generations, exhaustion and four-pass cleanup OK");return 85;
}
