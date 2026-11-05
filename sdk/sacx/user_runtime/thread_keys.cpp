#include "sacx_user_keys.h"
#include "sacx_user_sync.hpp"
#ifdef DIHOS_USER_SYNC_TEST
[[noreturn]] void dihos_user_key_panic(const char*);
#else
#include "sacx_user_api.h"
[[noreturn]] static void dihos_user_key_panic(const char *text){sacx_user_exit(159,text);}
#endif
namespace {
constexpr unsigned keys_count=16;
struct Key {uint32_t generation;bool active;void (*destructor)(void*);};
struct Value {uint32_t generation;void *pointer;};
Key keys[keys_count];sacx_user_mutex mutex;
thread_local Value values[keys_count];
struct Lock {
    Lock(){if(sacx_mutex_lock(&mutex))dihos_user_key_panic("SACX thread key lock failed");}
    ~Lock(){if(sacx_mutex_unlock(&mutex))dihos_user_key_panic("SACX thread key unlock failed");}
};
Key *lookup(sacx_user_key key){
    unsigned index=key&255;
    if(index>=keys_count || !keys[index].active || keys[index].generation!=(key>>8))return nullptr;
    return &keys[index];
}
}
extern "C" {
int sacx_user_key_create(sacx_user_key *output,void (*destructor)(void*)){
    if(!output)return 22;
    Lock lock;
    for(unsigned i=0;i<keys_count;++i){auto &key=keys[i];
        if(key.active || key.generation==0xffffff)continue;
        ++key.generation;key.active=true;key.destructor=destructor;
        *output=(key.generation<<8)|i;return 0;
    }
    return 11;
}
int sacx_user_key_delete(sacx_user_key id){
    Lock lock;Key *key=lookup(id);if(!key)return 22;
    key->active=false;key->destructor=nullptr;return 0;
}
int sacx_user_key_set(sacx_user_key id,void *pointer){
    Lock lock;if(!lookup(id))return 22;
    values[id&255]={id>>8,pointer};return 0;
}
void *sacx_user_key_get(sacx_user_key id){
    Lock lock;if(!lookup(id))return nullptr;
    auto &value=values[id&255];return value.generation==(id>>8)?value.pointer:nullptr;
}
void dihos_user_thread_keys_finalize(){
    // C++ thread_local destructors run before this phase. Clear before calling,
    // never call user code under the registry lock, permit four reinstall passes.
    for(unsigned pass=0;pass<4;++pass){
        bool called=false;
        for(unsigned i=0;i<keys_count;++i){
            void (*destructor)(void*)=nullptr;void *pointer=nullptr;
            {
                Lock lock;auto &key=keys[i];auto &value=values[i];
                if(key.active && value.generation==key.generation){
                    if(value.pointer && key.destructor){destructor=key.destructor;pointer=value.pointer;}
                }
                value.pointer=nullptr;
            }
            if(destructor){called=true;destructor(pointer);}
        }
        if(!called)break;
    }
    // Persistent reinstallers do not turn thread exit into an unbounded loop.
    for(auto &value:values)value={};
}
}
