// ARM64 Itanium C++ ABI lifetime support over native SACX threads.
#include "sacx_user_threads.h"
#include "sacx_user_api.h"
#include <stdint.h>
namespace {
struct Destructor {void (*function)(void*);void *argument;};
// No heap allocation or mutable initializer pointers in the TLS image.
thread_local Destructor destructors[32];
thread_local unsigned count;
uint32_t owner(){
    // TCB[1] is the process-local handle installed by the ELF loader.
    auto tcb=static_cast<const uint64_t*>(__builtin_thread_pointer());
    return static_cast<uint32_t>(tcb[1]&255)+1;
}
}
extern "C" {
void dihos_user_thread_keys_finalize(void) __attribute__((weak));
int __cxa_thread_atexit_impl(void (*function)(void*),void *argument,void*){
    if(!function || count==32)return -1;
    destructors[count++]={function,argument};return 0;
}
int __cxa_thread_atexit(void (*function)(void*),void *argument,void *dso){
    // Compiler-generated callers do not check registration failure. Terminate
    // the process rather than silently lose a nontrivial object's destructor.
    int result=__cxa_thread_atexit_impl(function,argument,dso);
    if(result)sacx_user_exit(155,"C++ TLS destructor registry exhausted");
    return 0;
}
void dihos_user_thread_finalize(){
    // Pop before calling: destructors may register another destructor.
    while(count){auto entry=destructors[--count];entry.function(entry.argument);}
    if(dihos_user_thread_keys_finalize)dihos_user_thread_keys_finalize();
}
// AArch64 guard ABI: low bit of the first byte means initialized. The rest
// belongs to the implementation. Wait on the aligned low 32-bit word.
int __cxa_guard_acquire(uint64_t *guard){
    auto state=reinterpret_cast<uint32_t*>(guard);
    for(;;){
        uint32_t observed=__atomic_load_n(state,__ATOMIC_ACQUIRE);
        if(observed&1)return 0;
        if(!observed){
            uint32_t expected=0;
            if(__atomic_compare_exchange_n(state,&expected,0x100,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
                __atomic_store_n(state+1,owner(),__ATOMIC_RELAXED);return 1;
            }
            continue;
        }
        if(__atomic_load_n(state+1,__ATOMIC_RELAXED)==owner())
            sacx_user_exit(153,"C++ recursive local static initialization");
        int rc=sacx_user_wait32(state,0x100,60000);
        if(rc && rc!=-11 && rc!=-110)sacx_user_exit(152,"C++ static guard wait failed");
    }
}
void __cxa_guard_release(uint64_t *guard){
    auto state=reinterpret_cast<uint32_t*>(guard);
    __atomic_store_n(state+1,0,__ATOMIC_RELAXED);
    __atomic_store_n(state,1,__ATOMIC_RELEASE);
    // This also supports single-threaded apps without the THREADS grant.
    (void)sacx_user_wake32(state,4);
}
void __cxa_guard_abort(uint64_t *guard){
    auto state=reinterpret_cast<uint32_t*>(guard);
    __atomic_store_n(state+1,0,__ATOMIC_RELAXED);
    __atomic_store_n(state,0,__ATOMIC_RELEASE);
    (void)sacx_user_wake32(state,4);
}
}
