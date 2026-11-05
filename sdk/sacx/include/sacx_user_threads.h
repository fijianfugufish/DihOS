#pragma once
#include "sacx_user_syscall.h"
typedef uint64_t sacx_user_thread;
typedef uint64_t (*sacx_user_thread_function)(uint64_t);
// Optional C++ runtime hook; plain C programs have no dependency on it.
#ifdef __cplusplus
extern "C" {
#endif
void dihos_user_thread_finalize(void) __attribute__((weak));
#ifdef __cplusplus
}
#endif
static inline __attribute__((noreturn)) void sacx_user_thread_exit(uint64_t result){
    if(dihos_user_thread_finalize)dihos_user_thread_finalize();
    (void)sacx_user_call(SACX_USER_THREAD_EXIT,result,0,0);
    for(;;)(void)sacx_user_call(SACX_USER_YIELD,0,0,0);
}
static __attribute__((noreturn)) void sacx_user_thread_start(uint64_t function,uint64_t argument){
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    uint64_t result=((sacx_user_thread_function)(uintptr_t)function)(argument);
    sacx_user_thread_exit(result);
}
static inline int64_t sacx_user_thread_create(sacx_user_thread_function function,uint64_t argument){
    if(!function)return -22;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return sacx_user_call(SACX_USER_THREAD_CREATE,(uintptr_t)&sacx_user_thread_start,(uintptr_t)function,argument);
}
static inline uint64_t sacx_user_thread_self(void){return (uint64_t)sacx_user_call(SACX_USER_THREAD_SELF,0,0,0);}
static inline int sacx_user_thread_join(sacx_user_thread thread,uint64_t *result,uint32_t timeout_ms){
    int rc=(int)sacx_user_call(SACX_USER_THREAD_JOIN,thread,(uintptr_t)result,timeout_ms);
    if(!rc)__atomic_thread_fence(__ATOMIC_ACQUIRE);return rc;
}
static inline int sacx_user_thread_detach(sacx_user_thread thread){
    return (int)sacx_user_call(SACX_USER_THREAD_DETACH,thread,0,0);
}
static inline int sacx_user_wait32(uint32_t *word,uint32_t expected,uint32_t timeout_ms){
    return (int)sacx_user_call(SACX_USER_WAIT32,(uintptr_t)word,expected,timeout_ms);
}
static inline int sacx_user_wake32(uint32_t *word,uint32_t count){
    return (int)sacx_user_call(SACX_USER_WAKE32,(uintptr_t)word,count,0);
}
